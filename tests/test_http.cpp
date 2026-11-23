#include "http_fixture.h"
#include "../runtime/include/http_client.h"
#include <csignal>
#ifdef AXON_HTTP_TEST_TLS
#include "http_tls_fixture.h"
#include <curl/curl.h>
#endif

using axon::AxonValue;
static AxonValue str(const std::string& s) {return AxonValue::from(s);}
static AxonValue opts() {AxonValue v;v.kind=AxonValue::Kind::Dict;v.dict["proxy"]=str("");return v;}
static AxonValue call(const std::string& name,const std::vector<AxonValue>& args,
                     std::shared_ptr<const axon::ExecutionControl> control=nullptr) {return axon::http_module_call(name,args,std::move(control));}
static void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static void error(const std::function<void()>& action,const std::string& text="HttpError:") {
    bool caught=false;try {action();} catch(const std::exception& e) {caught=std::string(e.what()).find(text)!=std::string::npos;}
    require(caught,"expected error was not reported");
}
int main(int argc,char** argv) {
#ifndef AXON_OS_WINDOWS
    std::signal(SIGPIPE,SIG_IGN); // Test TLS server peers intentionally reject/close connections.
#endif
    int passed=0,failed=0;
    auto test=[&](const char* name,const std::function<void()>& action) {
        try {action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e) {++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    };
    test("GET response and repeated headers",[] {
        HttpFixture server([](const std::string& request) {require(request.find("GET /data ")==0,"GET path");return HttpFixture::response("hello",200,"X-Test: one\r\nX-Test: two\r\n");});
        auto response=call("get",{str(server.url("/data")),opts()});
        require(response.dict.at("status").n==200&&response.dict.at("ok").b,"status/ok");
        require(response.dict.at("body").s=="hello","body");
        require(response.dict.at("headers").dict.at("x-test").list.size()==2,"repeated headers");
    });
    test("POST raw body including NUL",[] {
        std::string bytes("a\0b",3);
        HttpFixture server([&](const std::string& request) {require(request.substr(request.find("\r\n\r\n")+4)==bytes,"POST bytes");return HttpFixture::response("ok");});
        require(call("post",{str(server.url()),str(bytes),opts()}).dict.at("ok").b,"raw POST");
    });
    test("POST JSON and parse response",[] {
        HttpFixture server([](const std::string& request) {require(request.find("content-type: application/json")!=std::string::npos,"JSON content type");return HttpFixture::response("{\"received\":true}");});
        AxonValue body;body.kind=AxonValue::Kind::Dict;body.dict["answer"]=AxonValue::from(42.0);
        auto response=call("post",{str(server.url()),body,opts()});
        require(call("json",{response}).dict.at("received").b,"JSON decode");
    });
    test("non-2xx keeps response",[] {
        HttpFixture server([](const std::string&) {return HttpFixture::response("{\"error\":\"missing\"}",404);});
        auto response=call("get",{str(server.url()),opts()});
        require(response.dict.at("status").n==404&&!response.dict.at("ok").b,"404 status");
        require(call("json",{response}).dict.at("error").s=="missing","404 body");
        error([&]{call("json",{str(server.url()),opts()});},"HTTP 404");
    });
    test("JSON Unicode and malformed rejection",[] {
        AxonValue response;response.kind=AxonValue::Kind::Dict;response.dict["body"]=str("{\"text\":\"\\u00f1\\ud83d\\ude00\"}");
        require(call("json",{response}).dict.at("text").s==std::string("\xc3\xb1\xf0\x9f\x98\x80"),"Unicode");
        for(const std::string body:{"", "{\"key\":1", "[1,]", "true false", "01", "\"\\ud800\"", "1e9999"}) {
            response.dict["body"]=str(body);error([&]{call("json",{response});});
        }
    });
    test("body and header limits",[] {
        HttpFixture server([](const std::string&) {return HttpFixture::response(std::string(1000,'x'),200,"X-Large: "+std::string(200,'a')+"\r\n");});
        auto options=opts();options.dict["max_body_bytes"]=AxonValue::from(20.0);
        error([&]{call("get",{str(server.url()),options});},"body limit");
        options=opts();options.dict["max_header_bytes"]=AxonValue::from(64.0);
        error([&]{call("get",{str(server.url()),options});},"header limit");
    });
    test("incomplete body fails",[] {
        HttpFixture server([](const std::string&) {return "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort";});
        error([&]{call("get",{str(server.url()),opts()});});
    });
    test("redirect opt-in and loop limit",[] {
        HttpFixture server([](const std::string& request) {
            if(request.find("GET /target ")==0) return HttpFixture::response("final");
            return HttpFixture::response("redirect",302,"Location: /target\r\n");
        });
        require(call("get",{str(server.url()),opts()}).dict.at("status").n==302,"redirect default");
        auto options=opts();options.dict["follow_redirects"]=AxonValue::from(true);
        require(call("get",{str(server.url()),options}).dict.at("body").s=="final","follow redirect");
        HttpFixture loop([](const std::string&) {return HttpFixture::response("loop",302,"Location: /\r\n");});
        options.dict["max_redirects"]=AxonValue::from(1.0);
        error([&]{call("get",{str(loop.url()),options});},"redirect limit");
    });
    test("cross-origin redirects rejected",[] {
        HttpFixture server([](const std::string&) {return HttpFixture::response("redirect",302,"Location: http://localhost:1/\r\n");});
        auto options=opts();options.dict["follow_redirects"]=AxonValue::from(true);
        error([&]{call("get",{str(server.url()),options});},"cross-origin");
    });
    test("request deadline and parent deadline",[] {
        HttpFixture server([](const std::string&) {std::this_thread::sleep_for(std::chrono::milliseconds(150));return HttpFixture::response("late");});
        auto options=opts();options.dict["timeout"]=AxonValue::from(0.03);
        error([&]{call("get",{str(server.url()),options});},"TimeoutError:");
        options.dict["timeout"]=AxonValue::from(10.0);
        auto control=std::make_shared<axon::ExecutionControl>(axon::ExecutionControl().child(0.03,"parent"));
        error([&]{call("get",{str(server.url()),options},control);},"TimeoutError: parent");
    });
    test("host cancellation",[] {
        HttpFixture server([](const std::string&) {std::this_thread::sleep_for(std::chrono::milliseconds(150));return HttpFixture::response("late");});
        auto token=std::make_shared<std::atomic<bool>>(false);auto control=std::make_shared<axon::ExecutionControl>(token);
        std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(30));token->store(true);});
        try {error([&]{call("get",{str(server.url()),opts()},control);},"CancelledError:");} catch(...) {cancel.join();throw;}
        cancel.join();
    });
#ifdef AXON_HTTP_TEST_TLS
    (void)argc;(void)argv; // PKI and CRL endpoint are generated per run.
    test("HTTPS trusted fixture",[&] {
        HttpTlsFixture pki;
        HttpFixture server([](const std::string&){return HttpFixture::response("secure");},pki.certificate_file,pki.key_file);
        auto options=opts();options.dict["ca_file"]=str(pki.ca_file);
        require(call("get",{str(server.url()),options}).dict.at("body").s=="secure","TLS trust");
    });
    test("HTTPS untrusted CA and hostname mismatch",[&] {
        HttpTlsFixture pki;
        HttpFixture server([](const std::string&){return HttpFixture::response("secure");},pki.certificate_file,pki.key_file);
        error([&]{call("get",{str(server.url()),opts()});});
        auto options=opts();options.dict["ca_file"]=str(pki.ca_file);
        error([&]{call("get",{str("https://localhost:"+std::to_string(server.port)+"/"),options});},"name");
    });
    test("TLS fixture signed CRL and revoked certificate",[&] {
        HttpTlsFixture pki(true); // Constructor verifies rejection using OpenSSL CRL checks.
#ifdef AXON_OS_WINDOWS
        const auto* info=curl_version_info(CURLVERSION_NOW);
        if(info->ssl_version&&std::string(info->ssl_version).find("Schannel")!=std::string::npos) {
            HttpFixture server([](const std::string&){return HttpFixture::response("secure");},pki.certificate_file,pki.key_file);
            auto options=opts();options.dict["ca_file"]=str(pki.ca_file);
            error([&]{call("get",{str(server.url()),options});},"revoked");
        }
#endif
    });
#else
    (void)argc;(void)argv;
#endif
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
