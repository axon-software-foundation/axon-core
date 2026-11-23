/**
 * adapters/ollama/ollama_adapter.hpp
 * ────────────────────────────────────
 * Provider adapter for Ollama local LLM server.
 * Calls the Ollama REST API (default: http://localhost:11434).
 *
 * Uses a minimal interruptible TCP socket implementation for the local adapter.
 * General-purpose HTTP/HTTPS transport remains a separate module.
 *
 * Ollama API reference:
 *   POST /api/chat
 *   {"model":"llama3","messages":[{"role":"user","content":"..."}]}
 */

#pragma once
#include "../../runtime/interfaces/interfaces.hpp"
#include "../../runtime/include/platform.h"
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cerrno>

// ── Platform socket includes ─────────────────────────────────
#ifdef AXON_OS_WINDOWS
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
  #pragma comment(lib, "Ws2_32.lib")
  #endif
  using SocketFd = SOCKET;
  #define AXON_INVALID_SOCKET INVALID_SOCKET
  #define axon_close_socket closesocket
#else
  #include <fcntl.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using SocketFd = int;
  #define AXON_INVALID_SOCKET (-1)
  #define axon_close_socket   close
#endif

namespace axon {

// ─────────────────────────────────────────────────────────────
// Minimal JSON helpers (no dependency)
// ─────────────────────────────────────────────────────────────
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

// Extract a string value for a key from a flat JSON object.
// Simple scan — not a full JSON parser.
static std::string json_get(const std::string& json,
                             const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n'))
        ++pos;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // string value
        ++pos;
        std::string val;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos;
                switch (json[pos]) {
                    case 'n':  val += '\n'; break;
                    case 't':  val += '\t'; break;
                    case '"':  val += '"';  break;
                    case '\\': val += '\\'; break;
                    default:   val += json[pos];
                }
            } else {
                val += json[pos];
            }
            ++pos;
        }
        return val;
    }
    // number or bool
    std::string val;
    while (pos < json.size() && json[pos] != ','
           && json[pos] != '}' && json[pos] != '\n')
        val += json[pos++];
    // trim whitespace
    while (!val.empty() && val.back() == ' ') val.pop_back();
    return val;
}

// ─────────────────────────────────────────────────────────────
// Minimal blocking HTTP/1.1 POST
// ─────────────────────────────────────────────────────────────
class MinimalHttp {
public:
    struct Response {
        int status = 0;
        std::string body;
        std::string error;
    };

    static bool initialize() {
#ifdef AXON_OS_WINDOWS
        static const bool ready = [] { WSADATA wd; return WSAStartup(MAKEWORD(2,2), &wd) == 0; }();
        return ready;
#else
        return true;
#endif
    }

    static bool pending() {
#ifdef AXON_OS_WINDOWS
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
#else
        return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EINTR;
#endif
    }

    static Response post(const std::string& host, int port, const std::string& path,
                         const std::string& body, int timeout_ms = 30000,
                         std::shared_ptr<const ExecutionControl> inherited = nullptr,
                         const std::string& method = "POST") {
        Response response;
        // An explicit language budget replaces the default transport timeout.
        // With no language deadline, the transport still has a 30s default.
        ExecutionControl control = inherited ? *inherited : ExecutionControl();
        if (!control.has_deadline())
            control = control.child(timeout_ms / 1000.0, "Ollama request");
        control.check();
        if (!initialize()) { response.error = "Socket initialization failed"; return response; }
        addrinfo hints{}, *addresses = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        const std::string service = std::to_string(port);
        // OS hostname resolution is synchronous; it cannot be forcibly interrupted.
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) {
            response.error = "Could not resolve host: " + host;
            return response;
        }
        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> address_guard(addresses, freeaddrinfo);
        control.check();
        SocketFd socket_fd = socket(addresses->ai_family, addresses->ai_socktype, 0);
        if (socket_fd == AXON_INVALID_SOCKET) { response.error = "Socket creation failed"; return response; }
        struct SocketGuard {
            SocketFd fd;
            ~SocketGuard() { axon_close_socket(fd); }
        } socket_guard{socket_fd};
#ifdef AXON_OS_WINDOWS
        u_long nonblocking = 1;
        if (ioctlsocket(socket_fd, FIONBIO, &nonblocking) != 0) {
#else
        const int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
#endif
            response.error = "Could not enable interruptible socket I/O";
            return response;
        }
#ifndef AXON_OS_WINDOWS
        if (socket_fd >= FD_SETSIZE) { response.error = "Socket exceeds select limit"; return response; }
#endif
#ifdef SO_NOSIGPIPE
        int no_sigpipe = 1;
        setsockopt(socket_fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
        auto wait_ready = [&](bool write) {
            for (;;) {
                control.check();
                fd_set descriptors;
                FD_ZERO(&descriptors);
                FD_SET(socket_fd, &descriptors);
                timeval interval{0, 10000};
#ifdef AXON_OS_WINDOWS
                const int nfds = 0;
#else
                const int nfds = socket_fd + 1;
#endif
                int ready = select(nfds, write ? nullptr : &descriptors,
                                   write ? &descriptors : nullptr, nullptr, &interval);
                if (ready > 0) { control.check(); return true; }
                if (ready < 0 && !pending()) return false;
            }
        };
        if (connect(socket_fd, addresses->ai_addr, static_cast<int>(addresses->ai_addrlen)) != 0) {
            if (!pending() || !wait_ready(true)) {
                response.error = "Connection failed — is Ollama running? (ollama serve)";
                return response;
            }
            int error = 0;
#ifdef AXON_OS_WINDOWS
            int size = sizeof(error);
#else
            socklen_t size = sizeof(error);
#endif
            if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 || error) {
                response.error = "Connection failed — is Ollama running? (ollama serve)";
                return response;
            }
        }
        const std::string request = method + " " + path + " HTTP/1.1\r\nHost: " + host + ":" + service
            + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size())
            + "\r\nConnection: close\r\n\r\n" + body;
        size_t sent = 0;
        while (sent < request.size()) {
            if (!wait_ready(true)) { response.error = "Send failed"; return response; }
            int send_flags = 0;
#ifdef MSG_NOSIGNAL
            send_flags = MSG_NOSIGNAL;
#endif
            int count = send(socket_fd, request.data() + sent,
                             static_cast<int>(std::min<size_t>(request.size() - sent, 65536)), send_flags);
            if (count < 0 && pending()) continue;
            if (count <= 0) { response.error = "Send failed"; return response; }
            sent += static_cast<size_t>(count);
        }
        std::string raw;
        char buffer[4096];
        for (;;) {
            if (!wait_ready(false)) { response.error = "Receive failed"; return response; }
            int count = recv(socket_fd, buffer, sizeof(buffer), 0);
            if (count < 0 && pending()) continue;
            if (count < 0) { response.error = "Receive failed"; return response; }
            if (count == 0) break;
            raw.append(buffer, static_cast<size_t>(count));
        }
        control.check();
        const auto header_end = raw.find("\r\n\r\n");
        if (header_end == std::string::npos) { response.error = "Incomplete HTTP response"; return response; }
        std::istringstream status(raw.substr(0, raw.find("\r\n")));
        std::string protocol;
        status >> protocol >> response.status;
        if (protocol.compare(0, 5, "HTTP/") != 0 || response.status < 100 || response.status > 599) {
            response.error = "Invalid HTTP status";
            return response;
        }
        response.body = raw.substr(header_end + 4);
        std::string headers = raw.substr(0, header_end);
        std::transform(headers.begin(), headers.end(), headers.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto length_pos = headers.find("\r\ncontent-length:");
        if (length_pos != std::string::npos) {
            const std::string length = headers.substr(length_pos + 17);
            std::istringstream value(length);
            size_t expected = 0;
            if (!(value >> expected) || expected != response.body.size())
                response.error = "Incomplete HTTP body or invalid Content-Length";
        }
        return response;
    }
};

// ─────────────────────────────────────────────────────────────
// Ollama Adapter
// ─────────────────────────────────────────────────────────────
class OllamaAdapter final : public IProviderAdapter {
public:
    explicit OllamaAdapter(std::string host = "localhost",
                           int         port = 11434)
        : _host(std::move(host)), _port(port) {}

    std::string provider_name() const override { return "ollama"; }

    bool is_available() override {
        try {
            auto resp = MinimalHttp::post(_host, _port, "/api/tags", "", 3000, nullptr, "GET");
            return resp.error.empty() && resp.status == 200;
        } catch (const ExecutionInterrupted&) {
            return false;
        }
    }

    LLMResponse complete(const LLMRequest& req) override {
        auto t0 = std::chrono::steady_clock::now();

        // Build JSON body
        // {"model":"llama3","messages":[...],"stream":false}
        std::string messages_json;
        for (const auto& msg : req.messages) {
            if (!messages_json.empty()) messages_json += ",";
            messages_json +=
                "{\"role\":\"" + json_escape(msg.role) + "\","
                 "\"content\":\"" + json_escape(msg.content) + "\"}";
        }

        // Build options with compute config
        std::string opts = "{\"temperature\":" + std::to_string(req.temperature);
        if (req.max_tokens > 0)
            opts += ",\"num_predict\":" + std::to_string(req.max_tokens);
        if (req.num_ctx > 0)
            opts += ",\"num_ctx\":"    + std::to_string(req.num_ctx);
        // GPU/CPU control
        opts += ",\"num_gpu\":"        + std::to_string(req.num_gpu);
        if (req.main_gpu > 0)
            opts += ",\"main_gpu\":"   + std::to_string(req.main_gpu);
        if (req.num_thread > 0)
            opts += ",\"num_thread\":" + std::to_string(req.num_thread);
        if (req.num_batch > 0)
            opts += ",\"num_batch\":"  + std::to_string(req.num_batch);
        opts += ",\"f16_kv\":"         + std::string(req.f16_kv ? "true" : "false");
        if (req.low_vram)
            opts += ",\"low_vram\":true";
        opts += "}";

        std::string body =
            "{\"model\":\"" + json_escape(req.model) + "\","
             "\"messages\":[" + messages_json + "],"
             "\"stream\":false,"
             "\"options\":" + opts + "}";

        auto resp = MinimalHttp::post(_host, _port, "/api/chat", body, 30000, req.control);

        if (!resp.error.empty())
            throw std::runtime_error(
                "[OllamaAdapter] " + resp.error);

        if (resp.status != 200)
            throw std::runtime_error(
                "[OllamaAdapter] HTTP " + std::to_string(resp.status)
                + ": " + resp.body.substr(0, 200));

        // Extract content from:
        // {"message":{"role":"assistant","content":"..."},...}
        // Navigate to message.content
        std::string msg_block;
        auto msg_pos = resp.body.find("\"message\"");
        if (msg_pos != std::string::npos) {
            auto brace = resp.body.find('{', msg_pos);
            if (brace != std::string::npos) {
                // Find matching closing brace
                int depth = 0;
                bool quoted = false, escaped = false;
                for (size_t i = brace; i < resp.body.size(); ++i) {
                    const char c = resp.body[i];
                    if (escaped) { escaped = false; continue; }
                    if (quoted && c == '\\') { escaped = true; continue; }
                    if (c == '"') { quoted = !quoted; continue; }
                    if (quoted) continue;
                    if (c == '{') ++depth;
                    else if (resp.body[i] == '}') {
                        if (--depth == 0) {
                            msg_block = resp.body.substr(
                                brace, i - brace + 1);
                            break;
                        }
                    }
                }
            }
        }

        if (msg_block.empty() || msg_block.find("\"content\"") == std::string::npos)
            throw std::runtime_error("[OllamaAdapter] Invalid chat response: missing message.content");
        std::string content = json_get(msg_block, "content");
        if (content.empty())
            content = json_get(resp.body, "content");

        // Token counts (Ollama returns prompt_eval_count / eval_count)
        int tokens_in  = std::atoi(
            json_get(resp.body, "prompt_eval_count").c_str());
        int tokens_out = std::atoi(
            json_get(resp.body, "eval_count").c_str());

        auto t1 = std::chrono::steady_clock::now();
        double latency = std::chrono::duration<double, std::milli>(
            t1 - t0).count();

        return LLMResponse{
            content,
            tokens_in,
            tokens_out,
            latency,
            json_get(resp.body, "done_reason"),
        };
    }

private:
    std::string _host;
    int         _port;
};

} // namespace axon
