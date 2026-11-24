#include "sql_client.h"
#include <iostream>
#include <functional>
#include <filesystem>
#include <thread>
using axon::AxonValue;
using axon::SqlRuntime;
static AxonValue s(const std::string& value){return AxonValue::from(value);}
static AxonValue n(double value){return AxonValue::from(value);}
static AxonValue obj(){AxonValue v;v.kind=AxonValue::Kind::Dict;return v;}
static AxonValue values(std::initializer_list<AxonValue> items){AxonValue v;v.kind=AxonValue::Kind::List;v.list=items;return v;}
static void require(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
static void error(const std::function<void()>& call,const std::string& expected="SqlError:"){
    try{call();}catch(const std::exception& e){require(std::string(e.what()).find(expected)!=std::string::npos,e.what());return;}
    throw std::runtime_error("expected failure "+expected);
}
int main(){
    int passed=0,failed=0;
    auto test=[&](const char* name,const std::function<void()>& call){
        try{call();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    };
    test("connection names, close and runtime isolation",[]{
        SqlRuntime rt,other;auto cfg=obj();cfg.dict["name"]=s("primary");
        auto db=rt.call("connect",{cfg},nullptr,1);
        auto named=rt.call("connection",{s("primary")},nullptr,2);
        require(named.dict.at("__axon_sql_connection__").s==db.dict.at("__axon_sql_connection__").s,"named lookup");
        error([&]{rt.call("connect",{cfg},nullptr,1);},"already in use");
        error([&]{other.call("query",{db,s("SELECT 1")},nullptr,1);},"another runtime");
        require(rt.call("close",{db},nullptr,1).b,"close");
        require(!rt.call("close",{db},nullptr,1).b,"idempotent close");
        require(!rt.call("is_open",{db},nullptr,1).b,"closed state");
        error([&]{rt.call("query",{db,s("SELECT 1")},nullptr,1);},"closed");
        error([&]{rt.call("connection",{s("primary")},nullptr,1);},"unknown");
        for(int i=0;i<1030;++i) {auto next=rt.call("connect",{cfg},nullptr,1);rt.call("close",{next},nullptr,1);}
    });
    test("positional and named bound parameters prevent injection",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);
        rt.call("execute",{db,s("CREATE TABLE t(value TEXT)")},nullptr,1);
        rt.call("execute",{db,s("INSERT INTO t VALUES (?)"),values({s("x'); DROP TABLE t; --")})},nullptr,1);
        auto named=obj();named.dict["needle"]=s("x'); DROP TABLE t; --");
        auto rows=rt.call("query",{db,s("SELECT value FROM t WHERE value=:needle"),named},nullptr,1);
        require(rows.list.size()==1&&rows.list[0].dict.at("value").s==named.dict.at("needle").s,"bound text");
        error([&]{rt.call("query",{db,s("SELECT ?"),values({})},nullptr,1);},"parameter count");
        named.dict["unused"]=n(1);error([&]{rt.call("query",{db,s("SELECT :needle"),named},nullptr,1);},"unused");
    });
    test("types, NUL, blobs, null and large integers",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);
        AxonValue blob;blob.kind=AxonValue::Kind::Bytes;blob.bytes={0,255,1};
        auto row=rt.call("query",{db,s("SELECT ? AS text, ? AS blob, NULL AS missing, 9223372036854775807 AS large"),values({s(std::string("a\0b",3)),blob})},nullptr,1).list[0];
        require(row.dict.at("text").s==std::string("a\0b",3),"NUL");
        require(row.dict.at("blob").bytes==blob.bytes,"blob");
        require(row.dict.at("missing").is_null(),"NULL");
        require(row.dict.at("large").s=="9223372036854775807","large integer precision");
        auto empty=rt.call("query",{db,s("SELECT X'' AS empty")},nullptr,1).list[0].dict.at("empty");
        require(empty.kind==AxonValue::Kind::Bytes&&empty.bytes.empty(),"empty blob is not NULL");
        require(rt.call("query",{db,s("SELECT 1 WHERE 0")},nullptr,1).list.empty(),"empty rows");
    });
    test("transaction ownership, commit, rollback and owner cleanup",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);
        rt.call("execute",{db,s("CREATE TABLE t(value)")},nullptr,1);
        rt.call("begin",{db},nullptr,1);
        rt.call("execute",{db,s("INSERT INTO t VALUES (42)")},nullptr,1);
        error([&]{rt.call("query",{db,s("SELECT * FROM t")},nullptr,2);},"owned by another");
        rt.call("rollback",{db},nullptr,1);
        require(rt.call("query",{db,s("SELECT * FROM t")},nullptr,1).list.empty(),"rollback");
        rt.call("begin",{db},nullptr,1);rt.call("execute",{db,s("INSERT INTO t VALUES (7)")},nullptr,1);rt.call("commit",{db},nullptr,1);
        rt.call("begin",{db},nullptr,2);rt.call("execute",{db,s("INSERT INTO t VALUES (8)")},nullptr,2);rt.release_owner(2);
        require(rt.call("query",{db,s("SELECT * FROM t")},nullptr,1).list.size()==1,"owner cleanup rollback");
    });
    test("script, single statement guard and column aliases",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);
        auto results=rt.call("script",{db,s("CREATE TABLE t(value); INSERT INTO t VALUES ('}'); -- } comment\n SELECT value FROM t;")},nullptr,1);
        require(results.list.size()==3&&results.list.back().dict.at("rows").list[0].dict.at("value").s=="}","script");
        error([&]{rt.call("execute",{db,s("DELETE FROM t; SELECT 1")},nullptr,1);},"one statement");
        require(rt.call("query",{db,s("SELECT * FROM t; -- trailing comment")},nullptr,1).list.size()==1,"guard ran before DELETE");
        error([&]{rt.call("query",{db,s("SELECT 1 AS a, 2 AS a")},nullptr,1);},"duplicate result column");
    });
    test("row and byte limits",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);auto opts=obj();opts.dict["max_rows"]=n(2);
        error([&]{rt.call("query",{db,s("SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3"),values({}),opts},nullptr,1);},"row limit");
        opts=obj();opts.dict["max_bytes"]=n(16);
        error([&]{rt.call("query",{db,s("SELECT zeroblob(64) AS data"),values({}),opts},nullptr,1);},"byte limit");
    });
    test("query deadline, host cancellation and connection recovery",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);auto opts=obj();opts.dict["timeout"]=n(0.01);
        const auto query=s("WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<100000000) SELECT sum(x) AS total FROM n");
        error([&]{rt.call("query",{db,query,values({}),opts},nullptr,1);},"TimeoutError:");
        auto token=std::make_shared<std::atomic<bool>>(false);auto ctl=std::make_shared<axon::ExecutionControl>(token);
        std::thread cancel([&]{std::this_thread::sleep_for(std::chrono::milliseconds(20));token->store(true);});
        try{error([&]{rt.call("query",{db,query},ctl,1);},"CancelledError:");}catch(...){cancel.join();throw;}cancel.join();
        require(rt.call("query",{db,s("SELECT 42 AS answer")},nullptr,1).list[0].dict.at("answer").n==42,"recovery");
    });
    test("concurrent calls share a connection safely",[]{
        SqlRuntime rt;auto db=rt.call("connect",{obj()},nullptr,1);rt.call("execute",{db,s("CREATE TABLE t(value)")},nullptr,1);
        std::exception_ptr failures[4];std::vector<std::thread> threads;
        for(int k=0;k<4;++k) threads.emplace_back([&,k]{try{for(int i=0;i<50;++i)rt.call("execute",{db,s("INSERT INTO t VALUES (?)"),values({n(k)})},nullptr,static_cast<uintptr_t>(k+1));}catch(...){failures[k]=std::current_exception();}});
        for(auto& thread:threads)thread.join();
        for(auto& failure:failures)if(failure)std::rethrow_exception(failure);
        require(rt.call("query",{db,s("SELECT count(*) AS count FROM t")},nullptr,1).list[0].dict.at("count").n==200,"concurrent writes");
    });

    test("file persistence, readonly connection and foreign keys",[]{
        const auto path=std::filesystem::temp_directory_path()/("axon-sql-file-"+std::to_string(axon::ExecutionControl::Clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path),"create test directory");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{path};
        SqlRuntime rt;auto cfg=obj();cfg.dict["database"]=s((path/"data with spaces.db").string());
        auto db=rt.call("connect",{cfg},nullptr,1);
        rt.call("script",{db,s("CREATE TABLE parent(id PRIMARY KEY); CREATE TABLE child(parent REFERENCES parent(id)); INSERT INTO parent VALUES(42);")},nullptr,1);
        error([&]{rt.call("execute",{db,s("INSERT INTO child VALUES(99)")},nullptr,1);},"FOREIGN KEY");
        rt.call("close",{db},nullptr,1);cfg.dict["readonly"]=AxonValue::from(true);
        auto ro=rt.call("connect",{cfg},nullptr,1);
        require(rt.call("query",{ro,s("SELECT id FROM parent")},nullptr,1).list[0].dict.at("id").n==42,"persisted row");
        error([&]{rt.call("execute",{ro,s("INSERT INTO parent VALUES(1)")},nullptr,1);},"readonly");
    });
    test("database lock deadline does not block unrelated connections",[]{
        const auto path=std::filesystem::temp_directory_path()/("axon-sql-lock-"+std::to_string(axon::ExecutionControl::Clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path),"create test directory");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{path};
        SqlRuntime rt;auto cfg=obj();cfg.dict["database"]=s((path/"locked.db").string());
        auto first=rt.call("connect",{cfg},nullptr,1);auto second=rt.call("connect",{cfg},nullptr,2);
        auto independent=rt.call("connect",{obj()},nullptr,3);
        rt.call("execute",{first,s("CREATE TABLE t(value)")},nullptr,1);
        rt.call("begin",{first},nullptr,1);rt.call("execute",{first,s("INSERT INTO t VALUES(1)")},nullptr,1);
        auto opts=obj();opts.dict["timeout"]=n(0.06);
        std::exception_ptr failure;std::atomic<bool> started{false};
        std::thread waiter([&]{try{started=true;error([&]{rt.call("execute",{second,s("INSERT INTO t VALUES(2)"),values({}),opts},nullptr,2);},"TimeoutError:");}catch(...){failure=std::current_exception();}});
        while(!started.load())std::this_thread::yield();
        try{require(rt.call("query",{independent,s("SELECT 42 AS answer")},nullptr,3).list[0].dict.at("answer").n==42,"unrelated query");}catch(...){waiter.join();throw;}
        waiter.join();if(failure)std::rethrow_exception(failure);rt.call("rollback",{first},nullptr,1);
    });
    test("driver and configuration validation",[]{
        SqlRuntime rt;auto cfg=obj();cfg.dict["driver"]=s("duckdb");error([&]{rt.call("connect",{cfg},nullptr,1);},"not implemented");
        cfg=obj();cfg.dict["typo"]=n(1);error([&]{rt.call("connect",{cfg},nullptr,1);},"unknown connection option");
        error([&]{rt.call("query",{},nullptr,1);},"argument count");
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
