#include "../include/http_client.h"
#include "../include/document_io.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <limits>

namespace axon {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("HttpError: " + message); }
std::string lower(std::string text) {
    for (char& c : text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string trim(std::string text) {
    auto whitespace=[](unsigned char c) { return c==' ' || c=='\t' || c=='\r' || c=='\n'; };
    while (!text.empty() && whitespace(static_cast<unsigned char>(text.back()))) text.pop_back();
    size_t start=0; while (start<text.size() && whitespace(static_cast<unsigned char>(text[start]))) ++start;
    return text.substr(start);
}
// Kept private to HTTP: do not silently change the legacy json->parse contract.
class JsonReader {
    const std::string& text; size_t pos=0;
    void ws() { while(pos<text.size() && (text[pos]==' '||text[pos]=='\t'||text[pos]=='\r'||text[pos]=='\n')) ++pos; }
    char peek() const { return pos<text.size()?text[pos]:'\0'; }
    void take(char c) { if(peek()!=c) fail("invalid JSON"); ++pos; }
    unsigned hex4() {
        unsigned result=0;
        for(int i=0;i<4;++i) {
            if(pos>=text.size()) fail("incomplete JSON Unicode escape");
            unsigned char c=static_cast<unsigned char>(text[pos++]);
            result<<=4;
            if(c>='0'&&c<='9') result|=c-'0';
            else if(c>='a'&&c<='f') result|=c-'a'+10;
            else if(c>='A'&&c<='F') result|=c-'A'+10;
            else fail("invalid JSON Unicode escape");
        }
        return result;
    }
    static void utf8(std::string& out,unsigned cp) {
        if(cp<0x80) out+=static_cast<char>(cp);
        else if(cp<0x800) { out+=static_cast<char>(0xc0|(cp>>6)); out+=static_cast<char>(0x80|(cp&63)); }
        else if(cp<0x10000) { out+=static_cast<char>(0xe0|(cp>>12)); out+=static_cast<char>(0x80|((cp>>6)&63)); out+=static_cast<char>(0x80|(cp&63)); }
        else { out+=static_cast<char>(0xf0|(cp>>18)); out+=static_cast<char>(0x80|((cp>>12)&63)); out+=static_cast<char>(0x80|((cp>>6)&63)); out+=static_cast<char>(0x80|(cp&63)); }
    }
    std::string string() {
        take('"'); std::string out;
        while(pos<text.size()) {
            unsigned char c=static_cast<unsigned char>(text[pos++]);
            if(c=='"') return out;
            if(c<32) fail("unescaped JSON control character");
            if(c=='\\') {
                if(pos>=text.size()) fail("incomplete JSON escape");
                switch(text[pos++]) {
                    case '"':out+='"';break; case '\\':out+='\\';break; case '/':out+='/';break;
                    case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;
                    case 'r':out+='\r';break;case 't':out+='\t';break;
                    case 'u': {
                        unsigned cp=hex4();
                        if(cp>=0xd800 && cp<=0xdbff) {
                            take('\\');take('u');unsigned low=hex4();
                            if(low<0xdc00||low>0xdfff) fail("invalid JSON surrogate pair");
                            cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                        } else if(cp>=0xdc00&&cp<=0xdfff) fail("unpaired JSON surrogate");
                        utf8(out,cp);break;
                    }
                    default:fail("invalid JSON escape");
                }
            } else if(c<128) out+=static_cast<char>(c);
            else {
                unsigned cp=0,min=0;int extra=0;
                if(c>=0xc2&&c<=0xdf) {cp=c&31;min=0x80;extra=1;}
                else if(c>=0xe0&&c<=0xef) {cp=c&15;min=0x800;extra=2;}
                else if(c>=0xf0&&c<=0xf4) {cp=c&7;min=0x10000;extra=3;}
                else fail("invalid JSON UTF-8");
                for(int i=0;i<extra;++i) {
                    if(pos>=text.size()) fail("incomplete JSON UTF-8");
                    unsigned char next=static_cast<unsigned char>(text[pos++]);
                    if((next&0xc0)!=0x80) fail("invalid JSON UTF-8 continuation");
                    cp=(cp<<6)|(next&63);
                }
                if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) fail("invalid JSON code point");
                utf8(out,cp);
            }
        }
        fail("unterminated JSON string");
    }
    Value value(unsigned depth) {
        if(depth>128) fail("JSON nesting limit exceeded");
        ws(); char c=peek();
        if(c=='"') return Value::from(string());
        if(c=='{'||c=='[') {
            const bool object=c=='{';++pos;ws();
            Value out;out.kind=object?Value::Kind::Dict:Value::Kind::List;
            const char close=object?'}':']';
            if(peek()==close) {++pos;return out;}
            for(;;) {
                ws();std::string key;
                if(object) { key=string();ws();take(':'); }
                Value item=value(depth+1);
                if(object) out.dict[key]=std::move(item);else out.list.push_back(std::move(item));
                ws();if(peek()==close) {++pos;return out;}take(',');
            }
        }
        for(const auto& literal : {std::string("true"),std::string("false"),std::string("null")}) {
            if(text.compare(pos,literal.size(),literal)==0) {
                pos+=literal.size();return literal=="null"?Value::null():Value::from(literal=="true");
            }
        }
        const size_t start=pos;
        if(peek()=='-') ++pos;
        if(peek()=='0') ++pos;
        else {
            if(peek()<'1'||peek()>'9') fail("invalid JSON value");
            while(peek()>='0'&&peek()<='9') ++pos;
        }
        if(peek()=='.') {++pos;if(peek()<'0'||peek()>'9') fail("invalid JSON fraction");while(peek()>='0'&&peek()<='9') ++pos;}
        if(peek()=='e'||peek()=='E') {++pos;if(peek()=='+'||peek()=='-') ++pos;if(peek()<'0'||peek()>'9') fail("invalid JSON exponent");while(peek()>='0'&&peek()<='9') ++pos;}
        try {double number=std::stod(text.substr(start,pos-start));if(!std::isfinite(number)) fail("JSON number out of range");return Value::from(number);}
        catch(const std::out_of_range&) {fail("JSON number out of range");}
    }
public:
    explicit JsonReader(const std::string& input):text(input) {}
    Value parse() {Value result=value(0);ws();if(pos!=text.size()) fail("trailing JSON content");return result;}
};
struct GlobalCurl {
    GlobalCurl() {if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK) fail("curl initialization failed");}
    ~GlobalCurl() {curl_global_cleanup();}
};
void initialize() {static GlobalCurl instance; (void)instance;}
struct Url {
    std::unique_ptr<CURLU,decltype(&curl_url_cleanup)> handle{curl_url(),curl_url_cleanup};
    explicit Url(const std::string& text) {
        if(!handle || text.find('\0')!=std::string::npos || curl_url_set(handle.get(),CURLUPART_URL,text.c_str(),0)!=CURLUE_OK) fail("invalid URL");
        const auto scheme=part(CURLUPART_SCHEME);
        if(scheme!="http"&&scheme!="https") fail("only HTTP and HTTPS URLs are supported");
        if(part(CURLUPART_HOST).empty()||!part(CURLUPART_USER).empty()||!part(CURLUPART_PASSWORD).empty()) fail("URL must have a host and no embedded credentials");
    }
    std::string part(CURLUPart name,unsigned flags=0) const {
        char* value=nullptr;if(curl_url_get(handle.get(),name,&value,flags)!=CURLUE_OK) return "";
        std::string result=value;curl_free(value);return result;
    }
    std::string origin() const {return part(CURLUPART_SCHEME)+"://"+lower(part(CURLUPART_HOST))+":"+part(CURLUPART_PORT,CURLU_DEFAULT_PORT);}
};
struct Options {
    double timeout=30,connect_timeout=10;
    size_t max_body=8*1024*1024,max_headers=64*1024;
    bool follow=false;int redirects=5;
    std::string ca_file,proxy;bool set_proxy=false;
    std::map<std::string,std::string> headers;
};
double number(const Value& value,const std::string& name,double max) {
    if(value.kind!=Value::Kind::Number||!std::isfinite(value.n)||value.n<=0||value.n>max) fail("invalid option "+name);
    return value.n;
}
Options options(const Value* value) {
    Options out;if(!value) return out;if(value->kind!=Value::Kind::Dict) fail("options must be a dictionary");
    for(const auto& item:value->dict) {
        const auto& key=item.first;const auto& v=item.second;
        if(key=="timeout") out.timeout=number(v,key,3600);
        else if(key=="connect_timeout") out.connect_timeout=number(v,key,3600);
        else if(key=="max_body_bytes"||key=="max_header_bytes"||key=="max_redirects") {
            double max=key=="max_redirects"?10:(key=="max_body_bytes"?64*1024*1024:1024*1024);
            double n=number(v,key,max);if(std::floor(n)!=n) fail("option must be an integer: "+key);
            if(key=="max_body_bytes") out.max_body=static_cast<size_t>(n);
            else if(key=="max_header_bytes") out.max_headers=static_cast<size_t>(n);
            else out.redirects=static_cast<int>(n);
        } else if(key=="follow_redirects") {if(v.kind!=Value::Kind::Bool) fail("follow_redirects must be bool");out.follow=v.b;}
        else if(key=="ca_file"||key=="proxy") {
            if(v.kind!=Value::Kind::String||v.s.find('\0')!=std::string::npos) fail("invalid option "+key);
            if(key=="ca_file") out.ca_file=v.s;else {out.proxy=v.s;out.set_proxy=true;}
        } else if(key=="headers") {
            if(v.kind!=Value::Kind::Dict) fail("headers must be a dictionary");
            for(const auto& header:v.dict) {
                if(header.first.empty()||header.second.kind!=Value::Kind::String) fail("header names and values must be strings");
                for(unsigned char c:header.first)
                    if(c>=128||(!std::isalnum(c)&&std::string("!#$%&'*+-.^_`|~").find(static_cast<char>(c))==std::string::npos)) fail("invalid header name");
                if(header.second.s.find_first_of("\r\n")!=std::string::npos||header.second.s.find('\0')!=std::string::npos) fail("invalid header value");
                const std::string name=lower(header.first);
                if(name=="host"||name=="content-length"||name=="transfer-encoding") fail("request framing headers are managed by HTTP");
                if(out.headers.count(name)) fail("duplicate request header name");
                out.headers[name]=header.second.s;
            }
        } else fail("unknown option "+key);
    }
    return out;
}
struct Transfer {
    const Options& options;
    std::string body;
    std::map<std::string,std::vector<std::string>> headers;
    size_t body_bytes=0,header_bytes=0;
    std::exception_ptr callback_error;
    static size_t write(char* bytes,size_t size,size_t count,void* user) noexcept {
        auto& self=*static_cast<Transfer*>(user);
        try {
            if(size && count>std::numeric_limits<size_t>::max()/size) fail("body size overflow");
            size_t n=size*count;
            if(n>self.options.max_body-self.body_bytes) fail("response body limit exceeded");
            self.body_bytes+=n;self.body.append(bytes,n);return n;
        } catch(...) {self.callback_error=std::current_exception();return 0;}
    }
    static size_t header(char* bytes,size_t size,size_t count,void* user) noexcept {
        auto& self=*static_cast<Transfer*>(user);
        try {
            if(size && count>std::numeric_limits<size_t>::max()/size) fail("header size overflow");
            size_t n=size*count;if(n>self.options.max_headers-self.header_bytes) fail("response header limit exceeded");
            self.header_bytes+=n;std::string line(bytes,n);
            if(line.compare(0,5,"HTTP/")==0) self.headers.clear();
            else {auto colon=line.find(':');if(colon!=std::string::npos) self.headers[lower(trim(line.substr(0,colon)))].push_back(trim(line.substr(colon+1)));}
            return n;
        } catch(...) {self.callback_error=std::current_exception();return 0;}
    }
};
Value request(std::string url,std::string method,std::string body,Options opts,
              const std::shared_ptr<const ExecutionControl>& inherited) {
    initialize();Url initial(url);const std::string origin=initial.origin();
    if(body.size()>8*1024*1024) fail("request body limit exceeded");
    ExecutionControl control=(inherited?*inherited:ExecutionControl()).child(opts.timeout,"HTTP request");
    Transfer transfer{opts,{}, {},0,0,{}};
    for(int redirects=0;;++redirects) {
        control.check();transfer.body.clear();transfer.headers.clear();transfer.callback_error=nullptr;
        std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> easy(curl_easy_init(),curl_easy_cleanup);
        std::unique_ptr<CURLM,decltype(&curl_multi_cleanup)> multi(curl_multi_init(),curl_multi_cleanup);
        if(!easy||!multi) fail("cannot allocate HTTP transfer");
        auto set=[&](CURLoption option,auto value) {if(curl_easy_setopt(easy.get(),option,value)!=CURLE_OK) fail("curl option setup failed");};
        char error[CURL_ERROR_SIZE]{};set(CURLOPT_ERRORBUFFER,error);
        set(CURLOPT_URL,url.c_str());set(CURLOPT_NOSIGNAL,1L);
        set(CURLOPT_CONNECTTIMEOUT_MS,static_cast<long>(std::max(1.0,std::min(opts.connect_timeout,opts.timeout)*1000)));
        set(CURLOPT_SSL_VERIFYPEER,1L);set(CURLOPT_SSL_VERIFYHOST,2L);
        set(CURLOPT_SSLVERSION,static_cast<long>(CURL_SSLVERSION_TLSv1_2));
        set(CURLOPT_FOLLOWLOCATION,0L);set(CURLOPT_USERAGENT,"Axon-Core");
#if LIBCURL_VERSION_NUM >= 0x075500
        set(CURLOPT_PROTOCOLS_STR,"http,https");
#else
        set(CURLOPT_PROTOCOLS,static_cast<long>(CURLPROTO_HTTP|CURLPROTO_HTTPS));
#endif
        if(!opts.ca_file.empty()) set(CURLOPT_CAINFO,opts.ca_file.c_str());
        if(opts.set_proxy) set(CURLOPT_PROXY,opts.proxy.c_str());
        set(CURLOPT_WRITEFUNCTION,&Transfer::write);set(CURLOPT_WRITEDATA,&transfer);
        set(CURLOPT_HEADERFUNCTION,&Transfer::header);set(CURLOPT_HEADERDATA,&transfer);
        std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> headers(nullptr,curl_slist_free_all);
        auto append=[&](const std::string& header) {auto* next=curl_slist_append(headers.get(),header.c_str());if(!next) fail("cannot allocate request headers");headers.release();headers.reset(next);};
        for(const auto& header:opts.headers) append(header.first+": "+header.second);
        append("Expect:");set(CURLOPT_HTTPHEADER,headers.get());
        if(method=="POST") {set(CURLOPT_POST,1L);set(CURLOPT_POSTFIELDS,body.data());set(CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(body.size()));}
        if(curl_multi_add_handle(multi.get(),easy.get())!=CURLM_OK) fail("cannot schedule HTTP transfer");
        struct Remove {CURLM* multi;CURL* easy;~Remove(){curl_multi_remove_handle(multi,easy);}} remove{multi.get(),easy.get()};
        int running=1;
        while(running) {
            control.check();
            if(curl_multi_perform(multi.get(),&running)!=CURLM_OK) fail("HTTP transfer failed");
            if(running && curl_multi_poll(multi.get(),nullptr,0,20,nullptr)!=CURLM_OK) fail("HTTP poll failed");
        }
        control.check();if(transfer.callback_error) std::rethrow_exception(transfer.callback_error);
        int remaining=0;auto* message=curl_multi_info_read(multi.get(),&remaining);
        if(!message||message->msg!=CURLMSG_DONE) fail("missing transfer completion");
        if(message->data.result!=CURLE_OK) fail(error[0]?error:curl_easy_strerror(message->data.result));
        long status=0;curl_easy_getinfo(easy.get(),CURLINFO_RESPONSE_CODE,&status);
        const bool redirect=status==301||status==302||status==303||status==307||status==308;
        auto location=transfer.headers.find("location");
        if(opts.follow&&redirect&&location!=transfer.headers.end()&&!location->second.empty()) {
            if(redirects>=opts.redirects) fail("redirect limit exceeded");
            Url target(url);
            if(curl_url_set(target.handle.get(),CURLUPART_URL,location->second.back().c_str(),0)!=CURLUE_OK) fail("invalid redirect URL");
            const auto next=target.part(CURLUPART_URL);Url checked(next);
            if(checked.origin()!=origin) fail("cross-origin redirects are not supported");
            url=next;
            if(status==303||((status==301||status==302)&&method=="POST")) {method="GET";body.clear();}
            continue;
        }
        Value response;response.kind=Value::Kind::Dict;
        response.dict["status"]=Value::from(static_cast<double>(status));
        response.dict["ok"]=Value::from(status>=200&&status<300);
        response.dict["body"]=Value::from(transfer.body);response.dict["url"]=Value::from(url);
        Value values;values.kind=Value::Kind::Dict;
        for(const auto& item:transfer.headers) {
            Value list;list.kind=Value::Kind::List;
            for(const auto& value:item.second) list.list.push_back(Value::from(value));
            values.dict[item.first]=std::move(list);
        }
        response.dict["headers"]=std::move(values);return response;
    }
}
} // namespace

AxonValue http_module_call(const std::string& name,const std::vector<AxonValue>& args,
                          std::shared_ptr<const ExecutionControl> control) {
    if(name!="get"&&name!="post"&&name!="json") fail("unknown function "+name);
    const size_t minimum=name=="post"?2:1,maximum=minimum+1;
    if(args.size()<minimum||args.size()>maximum) fail("invalid argument count for "+name);
    if(name=="json"&&args[0].kind==Value::Kind::Dict) {
        auto body=args[0].dict.find("body");
        if(args.size()!=1||body==args[0].dict.end()||body->second.kind!=Value::Kind::String) fail("json(response) requires a string body");
        return JsonReader(body->second.s).parse();
    }
    if(args[0].kind!=Value::Kind::String) fail("URL must be a string");
    Options opts=options(args.size()>minimum?&args[minimum]:nullptr);
    std::string body,method="GET";
    if(name=="post") {
        method="POST";
        if(args[1].kind==Value::Kind::String) body=args[1].s;
        else {
            std::function<void(const Value&,unsigned)> validate=[&](const Value& value,unsigned depth) {
                if(depth>128||value.kind==Value::Kind::Bytes) fail("POST body must contain JSON values within the nesting limit");
                for(const auto& item:value.list) validate(item,depth+1);
                for(const auto& item:value.dict) validate(item.second,depth+1);
            };
            validate(args[1],0);
            body=json_stringify(args[1],0);JsonReader(body).parse();if(!opts.headers.count("content-type")) opts.headers["content-type"]="application/json";}
    }
    auto response=request(args[0].s,method,body,std::move(opts),control);
    if(name!="json") return response;
    if(!response.dict.at("ok").b) fail("HTTP "+std::to_string(static_cast<int>(response.dict.at("status").n))+" in json(url)");
    return JsonReader(response.dict.at("body").s).parse();
}
} // namespace axon
