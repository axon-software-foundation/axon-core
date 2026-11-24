#include "sql_client.h"
#include <sqlite3.h>
#include <mutex>
#include <map>
#include <set>
#include <cmath>
#include <limits>
#include <atomic>
#include <exception>

namespace axon {
namespace {
using Value=AxonValue;
[[noreturn]] void fail(const std::string& text) {throw std::runtime_error("SqlError: "+text);}
Value dict() {Value v;v.kind=Value::Kind::Dict;return v;}
Value list() {Value v;v.kind=Value::Kind::List;return v;}
std::string text(const Value& v,const std::string& field) {
    if(v.kind!=Value::Kind::String||v.s.find('\0')!=std::string::npos) fail(field+" must be a string without NUL");
    return v.s;
}
double positive(const Value& v,const std::string& field,double max) {
    if(v.kind!=Value::Kind::Number||!std::isfinite(v.n)||v.n<=0||v.n>max) fail("invalid "+field);
    return v.n;
}
Value integer(sqlite3_int64 value) {
    // Axon numbers are doubles. Preserve larger SQL integers as decimal strings.
    constexpr sqlite3_int64 exact=9007199254740991LL;
    return value>exact||value<-exact?Value::from(std::to_string(value)):Value::from(static_cast<double>(value));
}
struct Connection {
    std::timed_mutex mutex;
    sqlite3* db=nullptr;
    std::string name;
    uintptr_t transaction_owner=0;
    double busy_timeout=5;
    ~Connection() {if(db) sqlite3_close_v2(db);}
};
struct Operation {
    ExecutionControl control;
    double busy_timeout;
    std::exception_ptr interrupted;
    ExecutionControl::Clock::time_point started=ExecutionControl::Clock::now();
    static int progress(void* ptr) noexcept {
        auto& op=*static_cast<Operation*>(ptr);
        try {op.control.check();return 0;} catch(...) {op.interrupted=std::current_exception();return 1;}
    }
    static int busy(void* ptr,int attempts) noexcept {
        auto& op=*static_cast<Operation*>(ptr);
        try {
            op.control.check();
            if(attempts==0) op.started=ExecutionControl::Clock::now();
            if(std::chrono::duration<double>(ExecutionControl::Clock::now()-op.started).count()>=op.busy_timeout) return 0;
            op.control.sleep(0.01);return 1;
        } catch(...) {op.interrupted=std::current_exception();return 0;}
    }
};
struct Limits {double timeout=30;size_t rows=10000,bytes=8*1024*1024;};
Limits limits(const Value* value) {
    Limits out;if(!value) return out;
    if(value->kind!=Value::Kind::Dict) fail("query options must be a dictionary");
    for(const auto& item:value->dict) {
        if(item.first=="timeout") out.timeout=positive(item.second,item.first,3600);
        else if(item.first=="max_rows"||item.first=="max_bytes") {
            const double n=positive(item.second,item.first,item.first=="max_rows"?1000000:64*1024*1024);
            if(std::floor(n)!=n) fail(item.first+" must be an integer");
            if(item.first=="max_rows") out.rows=static_cast<size_t>(n);else out.bytes=static_cast<size_t>(n);
        } else fail("unknown query option "+item.first);
    }
    return out;
}
void bind(sqlite3_stmt* statement,const Value& params) {
    const int count=sqlite3_bind_parameter_count(statement);
    if(params.kind!=Value::Kind::List&&params.kind!=Value::Kind::Dict) fail("parameters must be a list or dictionary");
    if(params.kind==Value::Kind::List&&params.list.size()!=static_cast<size_t>(count)) fail("parameter count mismatch");
    std::set<std::string> used;
    for(int i=1;i<=count;++i) {
        const Value* value=nullptr;
        if(params.kind==Value::Kind::List) value=&params.list[static_cast<size_t>(i-1)];
        else {
            const char* raw=sqlite3_bind_parameter_name(statement,i);
            if(!raw) fail("unnamed parameter requires a list");
            std::string key=raw;
            auto it=params.dict.find(key);
            if(it==params.dict.end()&&(key[0]==':'||key[0]=='@'||key[0]=='$')) {key.erase(0,1);it=params.dict.find(key);}
            if(it==params.dict.end()) fail("missing parameter "+std::string(raw));
            used.insert(key);value=&it->second;
        }
        int rc=SQLITE_MISUSE;
        switch(value->kind) {
            case Value::Kind::Null:rc=sqlite3_bind_null(statement,i);break;
            case Value::Kind::Bool:rc=sqlite3_bind_int(statement,i,value->b?1:0);break;
            case Value::Kind::Number:
                if(!std::isfinite(value->n)) fail("non-finite SQL number");
                if(std::floor(value->n)==value->n&&std::abs(value->n)<=9007199254740991.0)
                    rc=sqlite3_bind_int64(statement,i,static_cast<sqlite3_int64>(value->n));
                else rc=sqlite3_bind_double(statement,i,value->n);
                break;
            case Value::Kind::String:
                rc=sqlite3_bind_text64(statement,i,value->s.data(),value->s.size(),SQLITE_TRANSIENT,SQLITE_UTF8);break;
            case Value::Kind::Bytes:
                rc=sqlite3_bind_blob64(statement,i,value->bytes.empty()?"":reinterpret_cast<const char*>(value->bytes.data()),value->bytes.size(),SQLITE_TRANSIENT);break;
            default:fail("SQL parameters must be scalar values or bytes");
        }
        if(rc!=SQLITE_OK) fail("cannot bind SQL parameter");
    }
    if(params.kind==Value::Kind::Dict&&used.size()!=params.dict.size()) fail("unused named parameter");
}
Value cell(sqlite3_stmt* statement,int i,size_t& bytes,size_t max) {
    Value value;
    auto consume=[&](size_t count) {
        if(bytes>max||count>max-bytes) fail("result byte limit exceeded");
        bytes+=count;
    };
    switch(sqlite3_column_type(statement,i)) {
        case SQLITE_NULL:consume(0);break;
        case SQLITE_INTEGER:consume(8);value=integer(sqlite3_column_int64(statement,i));break;
        case SQLITE_FLOAT:
            consume(8);value=Value::from(sqlite3_column_double(statement,i));
            if(!std::isfinite(value.n)) fail("non-finite SQL result");
            break;
        case SQLITE_TEXT: {
            const int n=sqlite3_column_bytes(statement,i);consume(static_cast<size_t>(n));
            const auto* raw=sqlite3_column_text(statement,i);
            if(raw) value=Value::from(std::string(reinterpret_cast<const char*>(raw),static_cast<size_t>(n)));
            else value=Value::from(std::string());
            break;
        }
        case SQLITE_BLOB: {
            const int n=sqlite3_column_bytes(statement,i);consume(static_cast<size_t>(n));
            value.kind=Value::Kind::Bytes;const auto* raw=static_cast<const uint8_t*>(sqlite3_column_blob(statement,i));
            if(n>0&&raw) value.bytes.assign(raw,raw+n);
            break;
        }
    }
    return value;
}
Value statement(Connection& conn,sqlite3_stmt* stmt,const Value& params,Operation& op,
                const Limits& bound,size_t& bytes,size_t& rows) {
    bind(stmt,params);
    Value result=dict(),columns=list(),data=list();std::set<std::string> names;
    for(int i=0;i<sqlite3_column_count(stmt);++i) {
        const std::string name=sqlite3_column_name(stmt,i);
        if(!names.insert(name).second) fail("duplicate result column; use SQL aliases");
        columns.list.push_back(Value::from(name));bytes+=name.size();
    }
    const sqlite3_int64 before=sqlite3_total_changes64(conn.db);
    for(;;) {
        op.control.check();const int rc=sqlite3_step(stmt);
        if(op.interrupted) std::rethrow_exception(op.interrupted);
        if(rc==SQLITE_DONE) break;
        if(rc!=SQLITE_ROW) fail(sqlite3_errmsg(conn.db));
        if(rows>=bound.rows) fail("result row limit exceeded");
        ++rows;Value row=dict();
        for(int i=0;i<sqlite3_column_count(stmt);++i) {
            const auto& name=columns.list[static_cast<size_t>(i)].s;bytes+=name.size();
            row.dict[name]=cell(stmt,i,bytes,bound.bytes);
            if(sqlite3_errcode(conn.db)==SQLITE_NOMEM) fail("out of memory reading SQL value");
        }
        data.list.push_back(std::move(row));
    }
    if(bytes>bound.bytes) fail("result byte limit exceeded");
    result.dict["rows"]=std::move(data);result.dict["columns"]=std::move(columns);
    result.dict["changes"]=integer(sqlite3_total_changes64(conn.db)-before);
    result.dict["last_insert_id"]=integer(sqlite3_last_insert_rowid(conn.db));
    result.dict["in_transaction"]=Value::from(sqlite3_get_autocommit(conn.db)==0);
    return result;
}
} // namespace
struct SqlRuntime::Impl {
    std::mutex mutex;
    std::map<std::string,std::shared_ptr<Connection>> connections;
    std::map<std::string,std::string> names;
    uint64_t next=0;
    const std::string prefix=std::to_string(reinterpret_cast<uintptr_t>(this))+":"+
        std::to_string(ExecutionControl::Clock::now().time_since_epoch().count())+":";
    std::shared_ptr<Connection> find(const Value& handle) {
        if(handle.kind!=Value::Kind::Dict) fail("expected a connection handle");
        auto token=handle.dict.find("__axon_sql_connection__");
        if(token==handle.dict.end()||token->second.kind!=Value::Kind::String) fail("invalid connection handle");
        std::lock_guard<std::mutex> lock(mutex);auto it=connections.find(token->second.s);
        if(it==connections.end()) {
            const auto& value=token->second.s;
            if(value.rfind(prefix,0)!=0) fail("connection belongs to another runtime or is invalid");
            const std::string suffix=value.substr(prefix.size());
            if(suffix.empty()||suffix.size()>20||suffix.find_first_not_of("0123456789")!=std::string::npos)
                fail("invalid connection handle");
            try {
                const auto id=std::stoull(suffix);
                if(id==0||id>next||std::to_string(id)!=suffix) fail("invalid connection handle");
            } catch(const std::out_of_range&) {fail("invalid connection handle");}
            return std::make_shared<Connection>(); // Closed handle; no registry tombstone needed.
        }
        return it->second;
    }
    Value handle(const std::string& token,const std::string& name) {
        Value out=dict();out.dict["__axon_sql_connection__"]=Value::from(token);
        out.dict["driver"]=Value::from(std::string("sqlite"));out.dict["name"]=Value::from(name);return out;
    }
};
SqlRuntime::SqlRuntime():impl(std::make_unique<Impl>()) {}
SqlRuntime::~SqlRuntime()=default;
Value SqlRuntime::call(const std::string& name,const std::vector<Value>& args,
                       std::shared_ptr<const ExecutionControl> inherited,uintptr_t owner) {
    ExecutionControl parent=inherited?*inherited:ExecutionControl();parent.check();
    if(name=="connect") {
        if(args.size()!=1||args[0].kind!=Value::Kind::Dict) fail("connect requires one configuration dictionary");
        std::string database=":memory:",label;bool readonly=false,create=true,foreign_keys=true;
        auto conn=std::make_shared<Connection>();
        for(const auto& item:args[0].dict) {
            if(item.first=="driver") {if(text(item.second,"driver")!="sqlite") fail("driver is not implemented; supported driver: sqlite");}
            else if(item.first=="database") database=text(item.second,"database");
            else if(item.first=="name") label=text(item.second,"name");
            else if(item.first=="busy_timeout") conn->busy_timeout=positive(item.second,"busy_timeout",3600);
            else if(item.first=="readonly"||item.first=="create"||item.first=="foreign_keys") {
                if(item.second.kind!=Value::Kind::Bool) fail(item.first+" must be bool");
                if(item.first=="readonly") readonly=item.second.b;else if(item.first=="create") create=item.second.b;else foreign_keys=item.second.b;
            } else fail("unknown connection option "+item.first);
        }
        if(database.empty()||database.rfind("file:",0)==0) fail("database must be a path or :memory:; SQLite URI mode is not enabled");
        const int flags=SQLITE_OPEN_FULLMUTEX|(readonly?SQLITE_OPEN_READONLY:(SQLITE_OPEN_READWRITE|(create?SQLITE_OPEN_CREATE:0)));
        const int rc=sqlite3_open_v2(database.c_str(),&conn->db,flags,nullptr);
        if(rc!=SQLITE_OK) fail("cannot open database: "+std::string(sqlite3_errstr(rc)));
        sqlite3_extended_result_codes(conn->db,1);
        sqlite3_limit(conn->db,SQLITE_LIMIT_LENGTH,16*1024*1024);
        sqlite3_limit(conn->db,SQLITE_LIMIT_SQL_LENGTH,1024*1024);
        sqlite3_limit(conn->db,SQLITE_LIMIT_VARIABLE_NUMBER,10000);
        if(sqlite3_exec(conn->db,foreign_keys?"PRAGMA foreign_keys=ON":"PRAGMA foreign_keys=OFF",nullptr,nullptr,nullptr)!=SQLITE_OK) fail("cannot configure foreign keys");
        conn->name=label;parent.check();
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!label.empty()&&impl->names.count(label)) fail("connection name already in use");
        if(impl->connections.size()>=1024) fail("connection registry limit exceeded");
        const std::string token=impl->prefix+std::to_string(++impl->next);
        impl->connections[token]=conn;if(!label.empty()) impl->names[label]=token;
        return impl->handle(token,label);
    }
    if(name=="connection") {
        if(args.size()!=1) fail("connection requires a name");
        const std::string label=text(args[0],"connection name");std::lock_guard<std::mutex> lock(impl->mutex);
        auto it=impl->names.find(label);if(it==impl->names.end()) fail("unknown connection name");return impl->handle(it->second,label);
    }
    const bool query=name=="query"||name=="execute",script=name=="script";
    const bool tx=name=="begin"||name=="commit"||name=="rollback";
    if(!query&&!script&&!tx&&name!="close"&&name!="is_open") fail("unknown function "+name);
    if((query&&(args.size()<2||args.size()>4))||(script&&(args.size()<2||args.size()>3))||
       ((!query&&!script)&&args.size()!=1)) fail("invalid argument count for "+name);
    const Limits bound=limits(query&&args.size()==4?&args[3]:(script&&args.size()==3?&args[2]:nullptr));
    Operation op{parent.child(bound.timeout,"SQL request"),0,{}};
    auto conn=impl->find(args[0]);std::unique_lock<std::timed_mutex> lock(conn->mutex,std::defer_lock);
    while(!lock.try_lock_for(std::chrono::milliseconds(10))) op.control.check();
    op.control.check();
    if(name=="is_open") return Value::from(conn->db!=nullptr);
    if(!conn->db) {if(name=="close") return Value::from(false);fail("connection is closed");}
    if(conn->transaction_owner&&conn->transaction_owner!=owner) fail("transaction is owned by another parallel branch");
    if(name=="close") {
        if(sqlite3_close(conn->db)!=SQLITE_OK) fail("cannot close connection");
        conn->db=nullptr;conn->transaction_owner=0;
        std::lock_guard<std::mutex> registry(impl->mutex);
        if(!conn->name.empty()) impl->names.erase(conn->name);
        impl->connections.erase(args[0].dict.at("__axon_sql_connection__").s);
        return Value::from(true);
    }
    op.busy_timeout=conn->busy_timeout;
    sqlite3_progress_handler(conn->db,1000,&Operation::progress,&op);
    sqlite3_busy_handler(conn->db,&Operation::busy,&op);
    struct Reset {Connection& conn;uintptr_t owner;~Reset(){
        sqlite3_progress_handler(conn.db,0,nullptr,nullptr);sqlite3_busy_handler(conn.db,nullptr,nullptr);
        conn.transaction_owner=sqlite3_get_autocommit(conn.db)?0:owner;
    }} reset{*conn,owner};
    std::string code=tx?(name=="begin"?"BEGIN":name=="commit"?"COMMIT":"ROLLBACK"):text(args[1],"SQL statement");
    if(code.empty()||code.size()>1024*1024) fail("SQL statement must contain 1 through 1048576 bytes");
    size_t rows=0,bytes=0;const char* tail=code.c_str();const char* end=tail+code.size();
    Value results=list(),parameters=query&&args.size()>=3?args[2]:list();
    while(tail<end) {
        op.control.check();
        // A preceding PRAGMA may replace SQLite's handler. Reinstall per statement.
        sqlite3_progress_handler(conn->db,1000,&Operation::progress,&op);
        sqlite3_busy_handler(conn->db,&Operation::busy,&op);
        sqlite3_stmt* raw=nullptr;const char* next=nullptr;
        const int rc=sqlite3_prepare_v3(conn->db,tail,static_cast<int>(end-tail),0,&raw,&next);
        std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> stmt(raw,sqlite3_finalize);
        if(op.interrupted) std::rethrow_exception(op.interrupted);
        if(rc!=SQLITE_OK) fail(sqlite3_errmsg(conn->db));
        if(!next||next<=tail) fail("SQL parser did not advance");
        tail=next;
        if(!stmt) continue;
        if(!script) {
            // Reject additional statements before the first statement can have side effects.
            const char* rest=tail;
            while(rest<end) {
                sqlite3_stmt* extra=nullptr;const char* after=nullptr;
                const int extra_rc=sqlite3_prepare_v3(conn->db,rest,static_cast<int>(end-rest),0,&extra,&after);
                std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> unused(extra,sqlite3_finalize);
                if(extra_rc!=SQLITE_OK||extra) fail("query/execute accepts one statement; use script for multiple statements");
                if(!after||after<=rest) break;
                rest=after;
            }
        }
        results.list.push_back(statement(*conn,stmt.get(),script?list():parameters,op,bound,bytes,rows));
    }
    if(results.list.empty()) fail("SQL contains no statement");
    op.control.check();
    if(script) return results;
    Value result=std::move(results.list.back());
    if(tx) return Value::from(true);
    return name=="query"?std::move(result.dict.at("rows")):result;
}
void SqlRuntime::release_owner(uintptr_t owner) noexcept {
    try {
        std::vector<std::shared_ptr<Connection>> all;
        {std::lock_guard<std::mutex> lock(impl->mutex);for(const auto& entry:impl->connections) all.push_back(entry.second);}
        for(auto& conn:all) {
            std::lock_guard<std::timed_mutex> lock(conn->mutex);
            if(conn->db&&conn->transaction_owner==owner) {sqlite3_exec(conn->db,"ROLLBACK",nullptr,nullptr,nullptr);conn->transaction_owner=0;}
        }
    } catch(...) {} // Destruction must not throw.
}
} // namespace axon
