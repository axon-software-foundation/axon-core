/**
 * adapters/ollama/ollama_adapter.hpp
 * ────────────────────────────────────
 * Provider adapter for Ollama local LLM server.
 * Calls the Ollama REST API (default: http://localhost:11434).
 *
 * No external HTTP libraries required for Phase 2B —
 * uses a minimal blocking TCP socket implementation.
 * Phase 4 will replace this with libcurl for full provider support.
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
        int         status = 0;
        std::string body;
        std::string error;
    };

    static Response post(const std::string& host,
                         int                port,
                         const std::string& path,
                         const std::string& body,
                         int                timeout_ms = 30000) {
        Response resp;

#ifdef AXON_OS_WINDOWS
        static bool wsa_init = false;
        if (!wsa_init) {
            WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
            wsa_init = true;
        }
#endif
        // Resolve host
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        std::string port_str = std::to_string(port);

        if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
            resp.error = "Could not resolve host: " + host;
            return resp;
        }

        SocketFd sock = socket(res->ai_family, res->ai_socktype, 0);
        if (sock == AXON_INVALID_SOCKET) {
            freeaddrinfo(res);
            resp.error = "Socket creation failed";
            return resp;
        }

        // Set receive timeout
#ifdef AXON_OS_WINDOWS
        DWORD tv = (DWORD)timeout_ms;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
            axon_close_socket(sock);
            freeaddrinfo(res);
            resp.error = "Connection refused — is Ollama running? "
                         "(ollama serve)";
            return resp;
        }
        freeaddrinfo(res);

        // Build HTTP request
        std::string request =
            "POST " + path + " HTTP/1.1\r\n"
            "Host: " + host + ":" + port_str + "\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        // Send
        size_t sent = 0;
        while (sent < request.size()) {
            int n = send(sock,
                         request.c_str() + sent,
                         (int)(request.size() - sent), 0);
            if (n <= 0) {
                axon_close_socket(sock);
                resp.error = "Send failed";
                return resp;
            }
            sent += n;
        }

        // Receive
        std::string raw;
        char buf[4096];
        while (true) {
            int n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            buf[n] = '\0';
            raw += buf;
        }
        axon_close_socket(sock);

        // Parse status line
        auto status_end = raw.find("\r\n");
        if (status_end != std::string::npos) {
            std::string status_line = raw.substr(0, status_end);
            auto sp1 = status_line.find(' ');
            if (sp1 != std::string::npos)
                resp.status = std::atoi(
                    status_line.c_str() + sp1 + 1);
        }

        // Find body (after \r\n\r\n)
        auto body_start = raw.find("\r\n\r\n");
        if (body_start != std::string::npos)
            resp.body = raw.substr(body_start + 4);

        return resp;
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
        // Quick health check — GET /api/tags
        auto resp = MinimalHttp::post(
            _host, _port, "/api/tags", "", 3000);
        // If connection fails, resp.error is non-empty
        return resp.error.empty();
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

        auto resp = MinimalHttp::post(_host, _port, "/api/chat", body);

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
                for (size_t i = brace; i < resp.body.size(); ++i) {
                    if (resp.body[i] == '{') ++depth;
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
