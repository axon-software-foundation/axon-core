#include "interop_process.h"
#include "document_io.h"
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <thread>
#include <vector>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
namespace axon {
namespace {
using Value = AxonValue;
namespace fs = std::filesystem;
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("InteropError: " + text); }
size_t setting(const char* name, size_t fallback, size_t maximum) {
    const char* raw = std::getenv(name);
    if (!raw) return fallback;
    std::string s(raw);
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) fail(std::string(name)+" must be a positive integer");
    try { const auto n=std::stoull(s); if (n>0 && n<=maximum) return static_cast<size_t>(n); } catch (...) {}
    fail(std::string(name)+" is outside its supported range");
}
struct Limits {
    size_t bytes=setting("AXON_INTEROP_MAX_BYTES",16*1024*1024,256*1024*1024);
    size_t output=setting("AXON_INTEROP_MAX_OUTPUT_BYTES",4*1024*1024,256*1024*1024);
    size_t depth=setting("AXON_INTEROP_MAX_DEPTH",64,128);
};
struct Temporary {
    fs::path path;
    Temporary() {
        static std::atomic<unsigned long> sequence{0};
        for (int i=0;i<64;++i) {
            path=fs::temp_directory_path()/fs::path("axon-interop-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1)));
            std::error_code e;
            if (fs::create_directory(path,e)) {
#ifndef _WIN32
                fs::permissions(path,fs::perms::owner_all,fs::perm_options::replace);
#endif
                return;
            }
        }
        fail("cannot create a private temporary directory");
    }
    ~Temporary() { std::error_code e; fs::remove_all(path,e); }
};
void write(const fs::path& path,const std::string& value) {
    std::ofstream f(path,std::ios::binary); f.write(value.data(),static_cast<std::streamsize>(value.size()));
    if (!f) fail("cannot write temporary protocol file");
}
std::string read(const fs::path& path,size_t limit) {
    std::error_code e; const auto size=fs::file_size(path,e);
    if (e || size>limit) fail("missing or oversized process output");
    std::ifstream f(path,std::ios::binary); std::string value(static_cast<size_t>(size),'\0');
    f.read(value.data(),static_cast<std::streamsize>(value.size()));
    if (!f) fail("cannot read process output");
    return value;
}
std::string encode(const Value& v,size_t depth,const Limits& limits) {
    if (depth>limits.depth) fail("input nesting limit exceeded");
    std::string result;
    switch(v.kind) {
    case Value::Kind::Bytes: fail("bytes cannot cross interop; convert explicitly to a list or file");
    case Value::Kind::Null: result="null"; break;
    case Value::Kind::Bool: result=v.b?"true":"false"; break;
    case Value::Kind::Number: {
        if (!std::isfinite(v.n) || (std::trunc(v.n)==v.n && std::abs(v.n)>9007199254740991.0)) fail("number is non-finite or outside the exact integer range");
        std::ostringstream s; s.imbue(std::locale::classic()); s<<std::setprecision(std::numeric_limits<double>::max_digits10)<<v.n; result=s.str();
        if(v.n==0 && std::signbit(v.n)) result="-0.0";
        break;
    }
    case Value::Kind::String: result=json_stringify(v,0); break;
    case Value::Kind::List:
        result="["; for (const auto& item:v.list) { if(result.size()>1) result+=","; result+=encode(item,depth+1,limits); if(result.size()>limits.bytes) fail("input byte limit exceeded"); } result+="]"; break;
    case Value::Kind::Dict:
        if(v.dict.count("__func__")||v.dict.count("__axon_sql_connection__")) fail("runtime handles/functions cannot cross interop");
        result="{"; for(const auto& item:v.dict) { if(result.size()>1) result+=","; result+=json_stringify(Value::from(item.first),0)+":"+encode(item.second,depth+1,limits); if(result.size()>limits.bytes) fail("input byte limit exceeded"); } result+="}"; break;
    }
    if(result.size()>limits.bytes) fail("input byte limit exceeded");
    return result;
}
#ifdef _WIN32
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n) fail("invalid UTF-8 process argument");
    std::wstring out(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
std::wstring quote(const std::string& raw) {
    const auto s=wide(raw); std::wstring out=L"\""; size_t slashes=0;
    for(wchar_t c:s) { if(c==L'\\') { ++slashes; continue; } if(c==L'"') out.append(slashes*2+1,L'\\'); else out.append(slashes,L'\\'); slashes=0; out+=c; }
    out.append(slashes*2,L'\\'); return out+L'"';
}
#endif
struct Completed { std::string out,err; int code; };
Completed execute(const std::vector<std::string>& args,const Temporary& temp,const Limits& limits,const ExecutionControl& control) {
    control.check();
    const auto stdout_path=temp.path/"stdout",stderr_path=temp.path/"stderr";
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    HANDLE in=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE out=CreateFileW(stdout_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE err=CreateFileW(stderr_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    auto close_files=[&] { for(HANDLE h:{in,out,err}) if(h!=INVALID_HANDLE_VALUE) CloseHandle(h); };
    if(in==INVALID_HANDLE_VALUE||out==INVALID_HANDLE_VALUE||err==INVALID_HANDLE_VALUE) {close_files();fail("cannot create child standard streams");}
    STARTUPINFOEXW startup{}; startup.StartupInfo.cb=sizeof(startup); startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput=in;startup.StartupInfo.hStdOutput=out;startup.StartupInfo.hStdError=err;
    SIZE_T size=0; InitializeProcThreadAttributeList(nullptr,1,0,&size); std::vector<unsigned char> attributes(size);
    startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    HANDLE handles[]={in,out,err};
    if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)) {close_files();fail("cannot initialize child handle list");}
    if(!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)) {DeleteProcThreadAttributeList(startup.lpAttributeList);close_files();fail("cannot restrict inherited handles");}
    std::wstring cmd; for(const auto& arg:args) { if(!cmd.empty())cmd+=L' ';cmd+=quote(arg); }
    PROCESS_INFORMATION process{};
    const BOOL ok=CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup.StartupInfo,&process);
    const DWORD launch_error=GetLastError();
    DeleteProcThreadAttributeList(startup.lpAttributeList); close_files();
    if(!ok) fail("cannot start '"+args[0]+"' (Windows error "+std::to_string(launch_error)+")");
    HANDLE job=CreateJobObjectW(nullptr,nullptr); JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{}; info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info))||!AssignProcessToJobObject(job,process.hProcess)) {
        TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,INFINITE); if(job)CloseHandle(job);CloseHandle(process.hThread);CloseHandle(process.hProcess);fail("cannot attach child process to cancellation job");
    }
    if(ResumeThread(process.hThread)==static_cast<DWORD>(-1)) {
        TerminateJobObject(job,1);WaitForSingleObject(process.hProcess,INFINITE);CloseHandle(job);CloseHandle(process.hThread);CloseHandle(process.hProcess);fail("cannot resume child process");
    }
    CloseHandle(process.hThread);
    auto stop=[&] { TerminateJobObject(job,1);WaitForSingleObject(process.hProcess,INFINITE);CloseHandle(job);CloseHandle(process.hProcess); };
#else
    posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,"/dev/null",O_RDONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,stdout_path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,stderr_path.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    posix_spawnattr_t attr;posix_spawnattr_init(&attr);posix_spawnattr_setflags(&attr,POSIX_SPAWN_SETPGROUP);posix_spawnattr_setpgroup(&attr,0);
    std::vector<char*> argv;for(const auto& a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    pid_t pid=0;const int launch_error=posix_spawnp(&pid,args[0].c_str(),&actions,&attr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attr);
    if(launch_error)fail("cannot start '"+args[0]+"' (OS error "+std::to_string(launch_error)+")");
    auto stop=[&] {kill(-pid,SIGKILL);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){};};
    int status=0;
#endif
    try {
        for(;;) {
            control.check();
            for(const auto& entry:std::vector<std::pair<fs::path,size_t>>{{stdout_path,limits.output},{stderr_path,limits.output},{temp.path/"response.json",limits.bytes}}) {
                std::error_code e;const auto n=fs::file_size(entry.first,e);if(!e&&n>entry.second)fail("process output byte limit exceeded");
            }
#ifdef _WIN32
            const auto state=WaitForSingleObject(process.hProcess,10);
            if(state==WAIT_OBJECT_0)break;
            if(state==WAIT_FAILED)fail("waiting for child process failed");
#else
            const auto state=waitpid(pid,&status,WNOHANG);
            if(state==pid)break;
            if(state<0&&errno!=EINTR)fail("waiting for child process failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
        }
    } catch(...) {stop();throw;}
#ifdef _WIN32
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(job);CloseHandle(process.hProcess);const int exit_code=static_cast<int>(code);
#else
    kill(-pid,SIGKILL); // Do not leave background descendants after the block exits.
    const int exit_code=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
    return {read(stdout_path,limits.output),read(stderr_path,limits.output),exit_code};
}
// Private strict protocol reader; legacy json->parse remains unchanged.
class InteropJsonReader {
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
        if(depth>256) fail("JSON nesting limit exceeded");
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
    explicit InteropJsonReader(const std::string& input):text(input) {}
    Value parse() {Value result=value(0);ws();if(pos!=text.size()) fail("trailing JSON content");return result;}
};
const char* runner=R"AXONPY(
import ast, json, keyword, math, os, sys, textwrap, traceback
request_path, response_path = sys.argv[1:3]
response = None
try:
    with open(request_path, encoding="utf-8") as f:
        request = json.load(f)
    limit, depth_limit = request["max_bytes"], request["max_depth"]
    def convert(value, depth=0, active=None):
        if depth > depth_limit:
            raise ValueError("interop nesting limit exceeded")
        t = type(value)
        if value is None or t is bool:
            return value
        if t is int:
            if abs(value) > 9007199254740991:
                raise ValueError("integer outside Axon's exact range")
            return value
        if t is float:
            if not math.isfinite(value) or (value.is_integer() and abs(value) > 9007199254740991):
                raise ValueError("non-finite number or integer outside Axon's exact range")
            return value
        if t is str:
            value.encode("utf-8", errors="strict")
            if len(value.encode("utf-8")) > limit:
                raise ValueError("interop string byte limit exceeded")
            return value
        if t not in (list, tuple, dict):
            raise TypeError("unsupported interop type: " + t.__name__)
        active = set() if active is None else active
        if id(value) in active:
            raise ValueError("cyclic interop container")
        active.add(id(value))
        try:
            if t is dict:
                if any(type(k) is not str for k in value):
                    raise TypeError("interop dictionary keys must be strings")
                return {convert(k, depth+1, active): convert(v, depth+1, active) for k,v in value.items()}
            return [convert(v, depth+1, active) for v in value]
        finally:
            active.remove(id(value))
    inputs = request["inputs"]
    for name in inputs:
        if not name.isidentifier() or keyword.iskeyword(name) or name.startswith("__axon_"):
            raise ValueError("invalid Python input name: " + name)
    inputs = convert(inputs)
    source = request["source"]
    sys.path.insert(0, os.path.dirname(source))
    body = ast.parse(textwrap.dedent(request["code"]), filename=source, mode="exec")
    ast.increment_lineno(body, request["line"] - 1)
    function = ast.FunctionDef(name="__axon_block", args=ast.arguments(posonlyargs=[],args=[ast.arg(arg=k) for k in inputs],kwonlyargs=[],kw_defaults=[],defaults=[]),body=body.body or [ast.Pass()],decorator_list=[])
    module = ast.fix_missing_locations(ast.Module(body=[function],type_ignores=[]))
    namespace = {"__name__":"__axon_interop__", "__file__":source}
    exec(compile(module, source, "exec"), namespace)
    result = convert(namespace["__axon_block"](**inputs))
    response = {"ok":True,"value":result}
except BaseException as error:
    response = {"ok":False,"error":type(error).__name__ + ": " + str(error),"traceback":traceback.format_exc()}
try:
    encoded = json.dumps(response, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(encoded) > request["max_bytes"]:
        raise ValueError("interop response byte limit exceeded")
    with open(response_path,"wb") as f:
        f.write(encoded)
except BaseException:
    traceback.print_exc()
    sys.exit(1)
)AXONPY";
ExecutionControl budget(const ExecutionControl& parent) {
    const char* raw=std::getenv("AXON_INTEROP_TIMEOUT");
    if(!raw)return parent;
    try {size_t used=0;const double seconds=std::stod(raw,&used);if(used!=std::string(raw).size()||!std::isfinite(seconds)||seconds<0)fail("AXON_INTEROP_TIMEOUT must be finite and non-negative");return parent.child(seconds,"interop");}
    catch(const ExecutionInterrupted&){throw;} catch(const std::exception& e){fail(std::string("invalid AXON_INTEROP_TIMEOUT: ")+e.what());}
}
} // namespace
AxonValue run_python_block(const std::string& code,const AxonValue& inputs,const std::string& source,int line,const ExecutionControl& parent,std::ostream& output,std::ostream& diagnostic) {
    const Limits limits;const auto control=budget(parent);control.check();Temporary temp;
    Value request;request.kind=Value::Kind::Dict;request.dict["inputs"]=inputs;request.dict["code"]=Value::from(code);
    request.dict["source"]=Value::from(fs::absolute(source.empty()?"<axon>":source).u8string());request.dict["line"]=Value::from(static_cast<double>(line));
    request.dict["max_bytes"]=Value::from(static_cast<double>(limits.bytes));request.dict["max_depth"]=Value::from(static_cast<double>(limits.depth));
    write(temp.path/"request.json",encode(request,0,limits));write(temp.path/"runner.py",runner);
    const char* configured=std::getenv("AXON_PYTHON");
#ifdef _WIN32
    std::string python=configured?configured:"python";
#else
    std::string python=configured?configured:"python3";
#endif
    if(python.empty()||python.find('\0')!=std::string::npos)fail("AXON_PYTHON must identify an executable, without extra arguments");
    const auto done=execute({python,"-X","utf8","-B","-u",(temp.path/"runner.py").u8string(),(temp.path/"request.json").u8string(),(temp.path/"response.json").u8string()},temp,limits,control);
    output<<done.out;diagnostic<<done.err;
    if(done.code!=0)fail("Python process exited with code "+std::to_string(done.code)
        +(done.err.empty()?"":"\n"+done.err.substr(done.err.size()>16384?done.err.size()-16384:0)));
    const std::string response=read(temp.path/"response.json",limits.bytes);
    // Protocol is generated by the trusted runner, never extracted from stdout.
    const Value envelope=InteropJsonReader(response).parse();
    if(envelope.kind!=Value::Kind::Dict||!envelope.dict.count("ok")||envelope.dict.at("ok").kind!=Value::Kind::Bool)fail("invalid Python response envelope");
    if(!envelope.dict.at("ok").b) {
        const auto error=envelope.dict.find("error"),trace=envelope.dict.find("traceback");
        fail("Python "+(error==envelope.dict.end()?"execution failed":error->second.s)+"\n"+(trace==envelope.dict.end()?"":trace->second.s));
    }
    if(!envelope.dict.count("value"))fail("Python response has no value");
    control.check();return envelope.dict.at("value");
}
AxonValue run_shell_block(const std::string& code,const ExecutionControl& parent,std::ostream& output,std::ostream& diagnostic) {
    const Limits limits;const auto control=budget(parent);Temporary temp;
#ifdef _WIN32
    const char* configured=std::getenv("COMSPEC");const std::string shell=configured?configured:"cmd.exe";
    // Batch files otherwise echo commands and the working-directory prompt into stdout.
    write(temp.path/"script.cmd","@echo off\r\n"+code+"\r\n");
    const auto done=execute({shell,"/D","/C",(temp.path/"script.cmd").u8string()},temp,limits,control);
#else
    write(temp.path/"script.sh",code+"\n");
    const auto done=execute({"/bin/sh",(temp.path/"script.sh").string()},temp,limits,control);
#endif
    output<<done.out;diagnostic<<done.err;Value result;result.kind=Value::Kind::Dict;
    result.dict["stdout"]=Value::from(done.out);result.dict["stderr"]=Value::from(done.err);result.dict["exit_code"]=Value::from(static_cast<double>(done.code));return result;
}
} // namespace axon
