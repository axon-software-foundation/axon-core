// Real loopback socket tests; no Ollama, Python, model or external network required.
#include "../adapters/ollama/ollama_adapter.hpp"
#include <atomic>
#include <thread>
#include <functional>

using namespace axon;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class FixtureServer {
public:
    std::string request;
    int port = 0;
    FixtureServer(std::string reply, int delay_ms = 0, bool drip = false) {
        require(MinimalHttp::initialize(), "socket initialization");
        listener = socket(AF_INET, SOCK_STREAM, 0);
        require(listener != AXON_INVALID_SOCKET, "listener creation");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 1)) {
            axon_close_socket(listener); throw std::runtime_error("fixture bind/listen");
        }
#ifdef AXON_OS_WINDOWS
        int size = sizeof(address);
#else
        socklen_t size = sizeof(address);
#endif
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size)) {
            axon_close_socket(listener); throw std::runtime_error("fixture getsockname");
        }
        port = ntohs(address.sin_port);
        worker = std::thread([this, reply=std::move(reply), delay_ms, drip] {
            SocketFd client = AXON_INVALID_SOCKET;
            const auto end = ExecutionControl::Clock::now() + std::chrono::seconds(2);
            while (!stopped && ExecutionControl::Clock::now() < end) {
                fd_set set; FD_ZERO(&set); FD_SET(listener, &set);
                timeval interval{0, 10000};
#ifdef AXON_OS_WINDOWS
                const int nfds = 0;
#else
                const int nfds = listener + 1;
#endif
                if (select(nfds, &set, nullptr, nullptr, &interval) > 0) {
                    client = accept(listener, nullptr, nullptr); break;
                }
            }
            if (client == AXON_INVALID_SOCKET) return;
#ifdef AXON_OS_WINDOWS
            DWORD timeout = 500;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
#else
            timeval timeout{0, 500000};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
#ifdef SO_NOSIGPIPE
            int no_sigpipe = 1;
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
            char buffer[2048];
            while (!stopped) {
                int count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0) break;
                request.append(buffer, static_cast<size_t>(count));
                const auto headers = request.find("\r\n\r\n");
                const auto length = request.find("Content-Length:");
                if (headers != std::string::npos && length != std::string::npos) {
                    const size_t expected = static_cast<size_t>(std::stoul(request.substr(length + 15)));
                    if (request.size() >= headers + 4 + expected) break;
                }
            }
            for (int elapsed = 0; elapsed < delay_ms && !stopped; elapsed += 5)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            size_t sent = 0;
            while (sent < reply.size() && !stopped) {
                int flags = 0;
#ifdef MSG_NOSIGNAL
                flags = MSG_NOSIGNAL;
#endif
                int count = send(client, reply.data() + sent,
                                 static_cast<int>(drip ? 1 : reply.size() - sent), flags);
                if (count <= 0) break;
                sent += static_cast<size_t>(count);
                if (drip) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            axon_close_socket(client);
        });
    }
    void finish() { stopped = true; if (worker.joinable()) worker.join(); }
    ~FixtureServer() { finish(); axon_close_socket(listener); }
    FixtureServer(const FixtureServer&) = delete;
    FixtureServer& operator=(const FixtureServer&) = delete;
private:
    SocketFd listener = AXON_INVALID_SOCKET;
    std::atomic<bool> stopped{false};
    std::thread worker;
};

static std::string reply(const std::string& body, int status = 200) {
    return "HTTP/1.1 " + std::to_string(status) + " fixture\r\nContent-Length: "
        + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}
static void interrupted(const std::function<void()>& action, const char* expected) {
    const auto start = ExecutionControl::Clock::now();
    bool caught = false;
    try { action(); }
    catch (const ExecutionInterrupted& error) {
        caught = std::string(error.what()).find(expected) != std::string::npos;
    }
    require(caught, "expected interruption");
    require(ExecutionControl::Clock::now() - start < std::chrono::seconds(2), "interruption took too long");
}
int main() {
    int failed = 0, passed = 0;
    auto test = [&](const char* name, const std::function<void()>& action) {
        try { action(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("health uses GET and checks status", [] {
        FixtureServer server(reply("{}"));
        OllamaAdapter adapter("127.0.0.1", server.port);
        require(adapter.is_available(), "health failed"); server.finish();
        require(server.request.find("GET /api/tags HTTP/1.1") == 0, "health method/path");
    });
    test("unhealthy HTTP status", [] {
        FixtureServer server(reply("unavailable", 503));
        OllamaAdapter adapter("127.0.0.1", server.port);
        require(!adapter.is_available(), "503 treated as available");
    });
    test("real chat request and quoted braces", [] {
        FixtureServer server(reply("{\"message\":{\"role\":\"assistant\",\"content\":\"a } b { c\"},\"eval_count\":3}"));
        OllamaAdapter adapter("127.0.0.1", server.port);
        LLMRequest request; request.model="fixture";
        request.messages.push_back({"user", "original input", 0});
        require(adapter.complete(request).content == "a } b { c", "wrong chat content");
        server.finish();
        require(server.request.find("POST /api/chat HTTP/1.1") == 0, "chat method/path");
        require(server.request.find("original input") != std::string::npos, "lost input");
    });
    test("HTTP errors propagate", [] {
        FixtureServer server(reply("model not found", 404));
        OllamaAdapter adapter("127.0.0.1", server.port);
        bool caught=false;
        try { adapter.complete(LLMRequest{}); }
        catch (const std::runtime_error& error) { caught=std::string(error.what()).find("HTTP 404")!=std::string::npos; }
        require(caught, "HTTP failure was hidden");
    });
    test("truncated response rejected", [] {
        FixtureServer server("HTTP/1.1 200 OK\r\nContent-Length: 500\r\n\r\n{}");
        auto response=MinimalHttp::post("127.0.0.1",server.port,"/api/chat","{}");
        require(!response.error.empty(), "truncated body accepted");
    });
    test("invalid chat response rejected", [] {
        FixtureServer server(reply("{}"));
        OllamaAdapter adapter("127.0.0.1",server.port);
        bool caught=false;
        try { adapter.complete(LLMRequest{}); } catch (const std::runtime_error&) { caught=true; }
        require(caught, "invalid chat accepted");
    });
    test("stalled request deadline", [] {
        FixtureServer server(reply("{}"),500);
        auto budget=std::make_shared<ExecutionControl>(ExecutionControl().child(0.03,"fixture"));
        interrupted([&] { MinimalHttp::post("127.0.0.1",server.port,"/api/chat","{}",30000,budget); },"TimeoutError: fixture");
    });
    test("slow bytes do not reset deadline", [] {
        FixtureServer server(reply("{}"),0,true);
        interrupted([&] { MinimalHttp::post("127.0.0.1",server.port,"/api/chat","{}",30); },"TimeoutError:");
    });
    test("host cancellation interrupts socket read", [] {
        FixtureServer server(reply("{}"),500);
        auto token=std::make_shared<std::atomic<bool>>(false);
        auto control=std::make_shared<ExecutionControl>(token);
        std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(30)); token->store(true); });
        try { interrupted([&] { MinimalHttp::post("127.0.0.1",server.port,"/api/chat","{}",30000,control); },"CancelledError:"); }
        catch (...) { canceller.join(); throw; }
        canceller.join();
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
