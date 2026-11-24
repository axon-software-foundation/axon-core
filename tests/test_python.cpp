#include "interop_process.h"
#include <iostream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <future>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <thread>
using axon::AxonValue;
namespace {
using Value=AxonValue;
Value object(){Value v;v.kind=Value::Kind::Dict;return v;}
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void error(const std::function<void()>& f,const std::string& expected){try{f();}catch(const std::exception& e){require(std::string(e.what()).find(expected)!=std::string::npos,e.what());return;}throw std::runtime_error("expected "+expected);}
void set(const char* key,const char* value){
#ifdef _WIN32
    _putenv_s(key,value?value:"");
#else
    if(value)setenv(key,value,1);else unsetenv(key);
#endif
}
struct Setting {
    const char* key;std::string previous;bool existed;
    Setting(const char* k,const char* v):key(k),previous(std::getenv(k)?std::getenv(k):""),existed(std::getenv(k)!=nullptr){set(key,v);}
    ~Setting(){set(key,existed?previous.c_str():nullptr);}
};
Value run(const std::string& code,const Value& inputs=object(),const axon::ExecutionControl& control=axon::ExecutionControl()){
    std::ostringstream out,err;return axon::run_python_block(code,inputs,"python-test.axon",1,control,out,err);
}
}
int main(){
    int passed=0,failed=0;
    auto test=[&](const char* name,const std::function<void()>& f){try{f();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    test("typed values, input precision and tuple normalization",[]{
        auto input=object();input.dict["x"]=Value::from(1.23456789012345);input.dict["text"]=Value::from(std::string("café 世界 😀")+std::string("\0x",2));
        auto r=run("return x, text, {'bool': True, 'none': None}",input);
        require(r.list.at(0).n==input.dict.at("x").n,"precision lost");require(r.list.at(1).s==input.dict.at("text").s,"UTF-8/NUL lost");require(r.list.at(2).dict.at("bool").b&&r.list.at(2).dict.at("none").is_null(),"types lost");
    });
    test("normal output cannot spoof return channel",[]{
        std::ostringstream out,err;auto r=axon::run_python_block("import sys\nprint('__COG__999')\nprint('diagnostic',file=sys.stderr)\nreturn 42",object(),"test.axon",1,axon::ExecutionControl(),out,err);
        require(r.n==42&&out.str().find("__COG__999")!=std::string::npos&&err.str().find("diagnostic")!=std::string::npos,"streams mixed");
    });
    test("exception traceback and explicit process exit",[]{
        error([]{run("raise RuntimeError('model failed')");},"RuntimeError: model failed");
        error([]{run("raise SyntaxError('syntax marker')");},"Traceback");
        error([]{run("import os\nos._exit(7)");},"exited with code 7");
    });
    test("unsupported types, dictionary keys, cycles and numeric range",[]{
        for(const std::string code:{"return object()","return b'bytes'","return {1,2}","return lambda x:x"})error([&]{run(code);},"unsupported interop type");
        error([]{run("return {1: 'bad'}");},"keys must be strings");
        error([]{run("x=[]\nx.append(x)\nreturn x");},"cyclic");
        error([]{run("return 2**53");},"exact range");
        error([]{run("return float('inf')");},"non-finite");
        auto input=object();input.dict["x"]=Value::from(9007199254740992.0);error([&]{run("return x",input);},"exact integer range");
        Value bytes;bytes.kind=Value::Kind::Bytes;input.dict["x"]=bytes;error([&]{run("return x",input);},"bytes cannot cross");
    });
    test("independent processes and parameter name validation",[]{
        require(run("hidden=42").is_null(),"implicit output");
        require(run("return 'hidden' in globals() or 'hidden' in locals()").b==false,"state leaked");
        auto input=object();input.dict["class"]=Value::from(1.0);error([&]{run("return 1",input);},"invalid Python input name");
    });
    test("parent deadline kills blocked child and recovers",[]{
        axon::ExecutionControl control;auto limited=control.child(0.15,"Python fixture");
        const auto start=std::chrono::steady_clock::now();error([&]{run("import time\ntime.sleep(30)",object(),limited);},"TimeoutError");
        require(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<3,"deadline did not kill child");require(run("return 42").n==42,"recovery");
    });
    test("host cancellation kills child",[]{
        auto token=std::make_shared<std::atomic<bool>>(false);axon::ExecutionControl control(token);
        auto job=std::async(std::launch::async,[&]{error([&]{run("import time\ntime.sleep(30)",object(),control);},"CancelledError");});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));token->store(true);require(job.wait_for(std::chrono::seconds(3))==std::future_status::ready,"cancellation stuck");job.get();
    });
    test("parallel subprocesses rendezvous without serialization",[]{
        namespace fs=std::filesystem;
        const auto directory=fs::temp_directory_path()/("axon-python-rendezvous-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(directory);
        struct Cleanup {fs::path path;~Cleanup(){std::error_code e;fs::remove_all(path,e);}} cleanup{directory};
        auto work=[&](const std::string& name,const std::string& peer,double value){
            auto inputs=object();inputs.dict["directory"]=Value::from(directory.u8string());inputs.dict["name"]=Value::from(name);inputs.dict["peer"]=Value::from(peer);inputs.dict["value"]=Value::from(value);
            return run("from pathlib import Path\nimport time\np=Path(directory)\n(p/name).write_text('ready')\nwhile not (p/peer).exists(): time.sleep(0.01)\nreturn value",inputs,axon::ExecutionControl().child(5,"rendezvous"));
        };
        auto a=std::async(std::launch::async,[&]{return work("a","b",20);});
        auto b=std::async(std::launch::async,[&]{return work("b","a",40);});
        require(a.get().n==20&&b.get().n==40,"results crossed");
    });
    test("configurable depth and byte limits",[]{
        {Setting setting("AXON_INTEROP_MAX_DEPTH","5");error([]{run("x=0\nfor i in range(8): x=[x]\nreturn x");},"nesting limit");}
        {Setting setting("AXON_INTEROP_MAX_BYTES","1024");error([]{run("return 'x'*4096");},"byte limit");}
        {Setting setting("AXON_INTEROP_MAX_OUTPUT_BYTES","512");error([]{run("print('x'*4096)\nreturn 1");},"output");}
        {Setting setting("AXON_INTEROP_MAX_DEPTH","wrong");error([]{run("return 1");},"positive integer");}
    });
    test("explicit executable and local timeout configuration",[]{
        {Setting setting("AXON_PYTHON","__axon_missing_python_executable__");error([]{run("return 1");},"cannot start");}
        {Setting setting("AXON_INTEROP_TIMEOUT","0.1");error([]{run("import time\ntime.sleep(30)");},"TimeoutError");}
        {Setting setting("AXON_INTEROP_TIMEOUT","NaN");error([]{run("return 1");},"TIMEOUT");}
    });
    test("shell captures independent streams and normalized status",[]{
        std::ostringstream out,err;auto r=axon::run_shell_block("echo hello\necho problem >&2\nexit 7",axon::ExecutionControl(),out,err);
        std::string stdout_text=r.dict.at("stdout").s;
        stdout_text.erase(std::remove(stdout_text.begin(),stdout_text.end(),'\r'),stdout_text.end());
        require(stdout_text=="hello\n","shell stdout includes command echo, prompt or unexpected text: "+stdout_text);
        require(r.dict.at("exit_code").n==7&&r.dict.at("stderr").s.find("problem")!=std::string::npos,"shell envelope");
    });
    test("shell deadline shares child cancellation backend",[]{
        std::ostringstream out,err;
#ifdef _WIN32
        const std::string command="ping -n 30 127.0.0.1 >nul";
#else
        const std::string command="sleep 30";
#endif
        error([&]{axon::run_shell_block(command,axon::ExecutionControl().child(0.15,"shell"),out,err);},"TimeoutError");
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
