#pragma once
#include "../adapters/ollama/ollama_adapter.hpp"
#include <atomic>
#include <functional>
#include <thread>
#ifdef AXON_HTTP_TEST_TLS
#include <openssl/ssl.h>
#endif

// Test-only loopback service. No external endpoints or production model responses.
class HttpFixture {
public:
    using Handler=std::function<std::string(const std::string&)>;
    std::atomic<int> active{0},requests{0};
    int port=0;
    explicit HttpFixture(Handler handler,const std::string& certificate="",const std::string& key="")
        : handler(std::move(handler)) {
        if(!axon::MinimalHttp::initialize()) throw std::runtime_error("fixture network initialization");
#ifdef AXON_HTTP_TEST_TLS
        if(!certificate.empty()) {
            context=SSL_CTX_new(TLS_server_method());
            if(!context||SSL_CTX_use_certificate_chain_file(context,certificate.c_str())!=1||
               SSL_CTX_use_PrivateKey_file(context,key.c_str(),SSL_FILETYPE_PEM)!=1) {
                if(context) SSL_CTX_free(context);
                throw std::runtime_error("fixture TLS setup");
            }
        }
#else
        (void)certificate;(void)key;
#endif
        listener=socket(AF_INET,SOCK_STREAM,0);
        if(listener==AXON_INVALID_SOCKET) throw std::runtime_error("fixture socket");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener,32)) {
            axon_close_socket(listener);throw std::runtime_error("fixture bind/listen");
        }
#ifdef AXON_OS_WINDOWS
        int length=sizeof(address);
#else
        socklen_t length=sizeof(address);
#endif
        if(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)) throw std::runtime_error("fixture port");
        port=ntohs(address.sin_port);
        acceptor=std::thread([this] {
            while(!stopped.load()) {
                fd_set set;FD_ZERO(&set);FD_SET(listener,&set);timeval interval{0,10000};
#ifdef AXON_OS_WINDOWS
                int nfds=0;
#else
                int nfds=listener+1;
#endif
                if(select(nfds,&set,nullptr,nullptr,&interval)<=0) continue;
                SocketFd client=accept(listener,nullptr,nullptr);
                if(client==AXON_INVALID_SOCKET) continue;
                workers.emplace_back([this,client] {serve(client);});
            }
        });
    }
    ~HttpFixture() {
        stopped=true;if(acceptor.joinable()) acceptor.join();
        for(auto& worker:workers) if(worker.joinable()) worker.join();
        axon_close_socket(listener);
#ifdef AXON_HTTP_TEST_TLS
        if(context) SSL_CTX_free(context);
#endif
    }
    std::string url(const std::string& path="/") const {
#ifdef AXON_HTTP_TEST_TLS
        const bool tls=context!=nullptr;
#else
        const bool tls=false;
#endif
        return std::string(tls?"https":"http")+"://127.0.0.1:"+std::to_string(port)+path;
    }
    static std::string response(const std::string& body,int status=200,const std::string& headers="") {
        return "HTTP/1.1 "+std::to_string(status)+" fixture\r\nContent-Length: "+std::to_string(body.size())
            +"\r\nConnection: close\r\n"+headers+"\r\n"+body;
    }
private:
    SocketFd listener=AXON_INVALID_SOCKET;
    Handler handler;
    std::atomic<bool> stopped{false};
    std::thread acceptor;std::vector<std::thread> workers;
#ifdef AXON_HTTP_TEST_TLS
    SSL_CTX* context=nullptr;
#endif
    void serve(SocketFd client) {
        struct Close {SocketFd fd;~Close(){axon_close_socket(fd);}} close{client};
#ifdef AXON_OS_WINDOWS
        DWORD timeout=2000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
#else
        timeval timeout{2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
#endif
#ifdef SO_NOSIGPIPE
        int yes=1;setsockopt(client,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
#endif
#ifdef AXON_HTTP_TEST_TLS
        std::unique_ptr<SSL,decltype(&SSL_free)> ssl(context?SSL_new(context):nullptr,SSL_free);
        if(ssl) {SSL_set_fd(ssl.get(),static_cast<int>(client));if(SSL_accept(ssl.get())!=1) return;}
#endif
        auto read=[&](char* bytes,int length) -> int {
#ifdef AXON_HTTP_TEST_TLS
            if(ssl) return SSL_read(ssl.get(),bytes,length);
#endif
            return static_cast<int>(recv(client,bytes,length,0));
        };
        auto write=[&](const char* bytes,int length) -> int {
#ifdef AXON_HTTP_TEST_TLS
            if(ssl) return SSL_write(ssl.get(),bytes,length);
#endif
            int flags=0;
#ifdef MSG_NOSIGNAL
            flags=MSG_NOSIGNAL;
#endif
            return static_cast<int>(send(client,bytes,length,flags));
        };
        std::string request;char bytes[4096];
        while(!stopped) {
            int n=read(bytes,sizeof(bytes));if(n<=0) return;request.append(bytes,static_cast<size_t>(n));
            const auto end=request.find("\r\n\r\n");
            if(end!=std::string::npos) {
                auto lower=request.substr(0,end);
                std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                const auto header=lower.find("content-length:");
                size_t length=0;
                try {if(header!=std::string::npos) length=static_cast<size_t>(std::stoul(lower.substr(header+15)));}
                catch(...) {return;}
                if(request.size()>=end+4+length) break;
            }
        }
        ++requests;++active;
        std::string response;
        try {response=handler(request);} catch(...) {response=HttpFixture::response("fixture failure",500);}
        --active;
        size_t sent=0;
        while(sent<response.size()&&!stopped) {
            int n=write(response.data()+sent,static_cast<int>(std::min<size_t>(65536,response.size()-sent)));
            if(n<=0) return;
            sent+=static_cast<size_t>(n);
        }
#ifdef AXON_HTTP_TEST_TLS
        if(ssl) SSL_shutdown(ssl.get());
#endif
    }
};
