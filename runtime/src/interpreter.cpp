#include <set>
#include <cstdlib>
#include <ctime>
#include <regex>
#include <chrono>
#include <thread>
#include <filesystem>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <fstream>
/**
 * runtime/src/interpreter.cpp
 * ────────────────────────────
 * Axon AST tree-walking interpreter.
 */

#include "platform.h"
#include "interpreter.h"
#include "memory_store.h"
#include "document_io.h"
#include "modules.h"
#include "http_client.h"
#include "lexer.h"
#include "axon_parser.h"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace axon {

namespace {
class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> action) : _action(std::move(action)) {}
    ~ScopeExit() { _action(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
private:
    std::function<void()> _action;
};

class TemporaryDirectory {
public:
    std::filesystem::path path;
    TemporaryDirectory() {
        static std::atomic<size_t> sequence{0};
        const auto base = std::filesystem::temp_directory_path();
        for (size_t i = 0; i < 64; ++i) {
            auto candidate = base / ("axon-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
                + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) { path = candidate; return; }
            if (error) throw std::runtime_error("cannot create Axon temporary directory: " + error.message());
        }
        throw std::runtime_error("cannot allocate Axon temporary directory");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

void validate_delay(double seconds, const std::string& owner) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error(owner + ": delay must be finite and non-negative");
}
} // namespace

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
std::string Interpreter::to_string(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Null:   return "null";
        case Value::Kind::Bool:   return v.b ? "true" : "false";
        case Value::Kind::Number: {
            if (v.n == (long long)v.n)
                return std::to_string((long long)v.n);
            std::ostringstream ss;
            ss << v.n;
            return ss.str();
        }
        case Value::Kind::String: return v.s;
        case Value::Kind::List: {
            std::string r = "[";
            for (size_t i = 0; i < v.list.size(); ++i) {
                if (i) r += ", ";
                const Value& item = v.list[i];
                if (item.kind == Value::Kind::String)
                    r += """ + item.s + """;
                else if (item.kind == Value::Kind::Dict || item.kind == Value::Kind::List)
                    r += to_string(item);
                else
                    r += to_string(item);
            }
            return r + "]";
        }
        case Value::Kind::Dict: {
            std::string r = "{";
            bool first = true;
            for (auto& [k, val] : v.dict) {
                if (!first) r += ", ";
                r += "\"" + k + "\": " + to_string(val);
                first = false;
            }
            return r + "}";
        }
        default: return "<bytes>";
    }
}

void Interpreter::log(const std::string& msg) {
    log(AXON_LOG_INFO, msg);
}

void Interpreter::log(AxonLogLevel level, const std::string& msg) {
    if (_out_cfg.format == AXON_OUT_SILENT) return;
    if (level < _out_cfg.level) return;
    // debug = false: suppress INFO and WARN runtime messages
    if (!_debug_mode && level <= AXON_LOG_WARN) return;

    std::string prefix, color_on = "", color_off = "";

#ifndef AXON_OS_WINDOWS
    color_off = "\033[0m";
    switch (level) {
        case AXON_LOG_DEBUG: color_on = "\033[90m";  prefix = "[debug]"; break;
        case AXON_LOG_INFO:  color_on = "\033[36m";  prefix = "[axon]";  break;
        case AXON_LOG_WARN:  color_on = "\033[33m";  prefix = "[warn]";  break;
        case AXON_LOG_ERROR: color_on = "\033[31m";  prefix = "[error]"; break;
    }
#else
    switch (level) {
        case AXON_LOG_DEBUG: prefix = "[debug]"; break;
        case AXON_LOG_INFO:  prefix = "[axon]";  break;
        case AXON_LOG_WARN:  prefix = "[warn]";  break;
        case AXON_LOG_ERROR: prefix = "[error]"; break;
    }
#endif

    std::string line;
    if (_out_cfg.format == AXON_OUT_JSON) {
        line = "{\"level\":\"" + prefix + "\",\"msg\":\"" + msg + "\"}";
    } else if (_out_cfg.format == AXON_OUT_MINIMAL) {
        line = msg;
    } else {
        line = color_on + prefix + color_off + " " + msg;
    }

    if (_out_cfg.to_stdout || (!_out_cfg.to_stdout && !_out_cfg.to_file))
        (*_stdout) << line << "\n";

    if (_out_cfg.to_file && _out_cfg.file_path) {
        auto file_guard = native_lock();
        std::ofstream f(_out_cfg.file_path, std::ios::app);
        if (f) f << line << "\n";
    }
}

void Interpreter::print_metrics() {
    (*_stdout) << "\n\033[1m── Metrics ─────────────────────────────────────\033[0m\n";
    for (auto& [name, def] : _agents) {
        auto retries = _retry_counts.count(name) ? _retry_counts[name] : 0;
        (*_stdout) << "  " << name
                  << "  retries=" << retries << "\n";
    }
    (*_stdout) << "────────────────────────────────────────────────\n";
}

// ── GAP 4: Condition evaluation ───────────────────────────────
bool Interpreter::eval_condition(const std::string& expr_str,
                                  const Value&       last_output,
                                  EnvPtr             env) {
    if (expr_str.empty()) return true;

    // Build evaluation env with output fields injected
    EnvPtr cond_env = child_env(env);
    cond_env->set("output", last_output);
    if (last_output.kind == Value::Kind::Dict) {
        for (auto& [k, v] : last_output.dict)
            cond_env->set(k, v);
    }
    // Also inject numeric output directly if it's a number
    if (last_output.kind == Value::Kind::Number)
        cond_env->set("value", last_output);

    // Lex + parse the condition string
    Lexer     lex(expr_str, "<condition>");
    auto      tokens = lex.tokenize();
    AxonArena* arena = axon_arena_new(4096);
    Parser    p(std::move(tokens), arena);
    AxonNode* expr = p.parse_expression();

    bool result = false;
    if (!p.has_errors() && expr)
        result = truthy(eval(expr, cond_env));

    axon_arena_free(arena);
    return result;
}

// ── GAP 3: JSON extraction from LLM output ────────────────────
std::string Interpreter::extract_json(const std::string& s) {
    // Strip markdown code blocks ```json ... ```
    auto start = s.find("```json");
    if (start != std::string::npos) {
        start = s.find('\n', start) + 1;
        auto end = s.find("```", start);
        if (end != std::string::npos)
            return s.substr(start, end - start);
    }
    // Try raw JSON: find first { or [
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '{' || s[i] == '[') {
            char close = (s[i] == '{') ? '}' : ']';
            int depth  = 0;
            for (size_t j = i; j < s.size(); ++j) {
                if (s[j] == s[i]) ++depth;
                else if (s[j] == close && --depth == 0)
                    return s.substr(i, j - i + 1);
            }
        }
    }
    return "";
}

// Simple JSON → AxonValue parser (objects and arrays of strings/numbers)
Value Interpreter::parse_json_value(const std::string& s) {
    std::string trimmed = s;
    while (!trimmed.empty() && (trimmed.front()==' '||trimmed.front()=='\n'))
        trimmed.erase(0,1);
    while (!trimmed.empty() && (trimmed.back()==' '||trimmed.back()=='\n'))
        trimmed.pop_back();

    if (trimmed.empty()) return Value::null();

    // String
    if (trimmed.front() == '"') {
        return Value::from(trimmed.substr(1,
            trimmed.size() > 2 ? trimmed.size()-2 : 0));
    }
    // Number
    if (std::isdigit((unsigned char)trimmed.front()) || trimmed.front() == '-') {
        try { return Value::from(std::stod(trimmed)); } catch (...) {}
    }
    // Bool
    if (trimmed == "true")  return Value::from(true);
    if (trimmed == "false") return Value::from(false);
    if (trimmed == "null")  return Value::null();

    // Object {key: val, ...}
    if (trimmed.front() == '{') {
        Value obj; obj.kind = Value::Kind::Dict;
        std::string inner = trimmed.substr(1, trimmed.size()-2);
        // Simple key extraction (not full JSON — handles flat objects)
        size_t i = 0;
        while (i < inner.size()) {
            while (i < inner.size() && inner[i] != '"') ++i;
            if (i >= inner.size()) break;
            ++i; // skip "
            std::string key;
            while (i < inner.size() && inner[i] != '"') key += inner[i++];
            ++i; // skip closing "
            while (i < inner.size() && (inner[i]==' '||inner[i]==':')) ++i;
            // read value until comma or end
            std::string val_str;
            int depth = 0;
            while (i < inner.size()) {
                char c = inner[i];
                if (c=='{' || c=='[') ++depth;
                if (c=='}' || c==']') --depth;
                if (c==',' && depth==0) { ++i; break; }
                val_str += c; ++i;
            }
            while (!val_str.empty() && (val_str.back()==' '||val_str.back()=='\n'))
                val_str.pop_back();
            obj.dict[key] = parse_json_value(val_str);
        }
        return obj;
    }

    // Default: treat as string
    return Value::from(trimmed);
}


// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
Interpreter::Interpreter()
    : _graph(std::make_unique<GraphEngine>())
    , _memory(std::make_unique<RuntimeMemory>())
    , _sessions(std::make_unique<SessionMemory>(100))
    , _global_env(make_env())
{
    // Default output config
    memset(&_out_cfg, 0, sizeof(_out_cfg));
    _out_cfg.format    = AXON_OUT_STRUCTURED;
    _out_cfg.level     = AXON_LOG_INFO;
    _out_cfg.to_stdout = 1;
    _out_cfg.metrics   = 0;
    // Inject built-in functions into global scope
    _global_env->set("print",  Value::from(std::string("__builtin_print__")));
    _global_env->set("sleep",  Value::from(std::string("sleep")));
    _global_env->set("typeof", Value::from(std::string("__builtin_typeof__")));
    _global_env->set("null",  Value::null());
    _global_env->set("true",  Value::from(true));
    _global_env->set("false", Value::from(false));
}

Interpreter::~Interpreter() {
    _shared->sql.release_owner(reinterpret_cast<uintptr_t>(this));
    for (AxonArena* arena : _import_arenas) axon_arena_free(arena);
}

void Interpreter::register_provider(
    std::shared_ptr<IProviderAdapter> adapter) {
    _provider_locks[adapter->provider_name()] = std::make_shared<std::timed_mutex>();
    _providers[adapter->provider_name()] = std::move(adapter);
}

// ─────────────────────────────────────────────────────────────
// run — entry point
// ─────────────────────────────────────────────────────────────
std::unique_lock<std::recursive_timed_mutex> Interpreter::native_lock() {
    std::unique_lock<std::recursive_timed_mutex> lock(_shared->native_mutex, std::defer_lock);
    while (!lock.try_lock_for(std::chrono::milliseconds(10))) _execution->check();
    _execution->check();
    return lock;
}

std::unique_ptr<Interpreter> Interpreter::fork_branch(EnvPtr env, EnvPtr& branch_env) {
    auto branch = std::make_unique<Interpreter>();
    branch->_shared = _shared;
    branch->_providers = _providers;
    branch->_provider_locks = _provider_locks;
    branch->_agents = _agents;
    branch->_graph = std::make_unique<GraphEngine>(*_graph);
    branch->_out_cfg = _out_cfg;
    branch->_storages = _storages;
    branch->_retry_counts = _retry_counts;
    branch->_source_file = _source_file;
    branch->_source_lines = _source_lines;
    branch->_debug_mode = _debug_mode;
    branch->_flow_delay = _flow_delay;
    branch->_flow_timeout = _flow_timeout;
    branch->_flows = _flows;
    branch->_graph_node = _graph_node;
    branch->_global_main_node = _global_main_node;
    branch->_expected_output = _expected_output;
    branch->_sessions = _sessions;
    branch->_cancelled = _cancelled;
    branch->_execution = _execution;
    branch->_closure_id_counter = _closure_id_counter;
    branch->telemetry = telemetry;
    // AST/definition pointers are borrowed until every worker has joined. Imported
    // arenas remain owned by the calling interpreter; never free them in a fork.
    std::unordered_map<size_t, size_t> ids;
    for (const auto& closure : _closure_envs)
        ids[closure.first] = _closure_id_counter->fetch_add(1, std::memory_order_relaxed);
    std::function<Value(const Value&)> copy_value = [&](const Value& value) {
        Value copied = value;
        for (auto& item : copied.list) item = copy_value(item);
        for (auto& item : copied.dict) item.second = copy_value(item.second);
        auto marker = copied.dict.find("__func__");
        auto cid = copied.dict.find("__closure_id__");
        if (marker != copied.dict.end() && marker->second.s == "func" && cid != copied.dict.end()) {
            auto found = ids.find(static_cast<size_t>(cid->second.n));
            if (found != ids.end()) cid->second = Value::from(static_cast<double>(found->second));
        }
        return copied;
    };
    std::unordered_map<const Env*, EnvPtr> environments;
    std::function<EnvPtr(EnvPtr)> copy_env = [&](EnvPtr source) -> EnvPtr {
        if (!source) return nullptr;
        auto found = environments.find(source.get());
        if (found != environments.end()) return found->second;
        auto copied = make_env();
        environments[source.get()] = copied;
        copied->_parent = copy_env(source->_parent);
        for (const auto& item : source->_vars) copied->set(item.first, copy_value(item.second));
        return copied;
    };
    branch->_global_env = copy_env(_global_env);
    branch_env = copy_env(env);
    for (const auto& closure : _closure_envs)
        branch->_closure_envs[ids.at(closure.first)] = copy_env(closure.second);
    for (const auto& output : _task_outputs)
        branch->_task_outputs[output.first] = copy_value(output.second);
    for (const auto* frame : _task_frames) {
        auto copied = std::make_unique<TaskFrame>();
        copied->agent = &branch->_agents.at(frame->agent->name);
        for (const auto& output : frame->outputs)
            copied->outputs[output.first] = copy_value(output.second);
        branch->_task_frames.push_back(copied.get());
        branch->_owned_task_frames.push_back(std::move(copied));
    }
    if (_current_agent) branch->_current_agent = &branch->_agents.at(_current_agent->name);
    return branch;
}

void Interpreter::import_branch_closures(const Interpreter& branch, const Value& value) {
    std::set<size_t> closures;
    std::set<const Env*> environments;
    std::function<void(const Value&)> visit_value;
    std::function<void(EnvPtr)> visit_env = [&](EnvPtr env) {
        if (!env || !environments.insert(env.get()).second) return;
        visit_env(env->_parent);
        for (const auto& item : env->_vars) visit_value(item.second);
    };
    visit_value = [&](const Value& item) {
        for (const auto& child : item.list) visit_value(child);
        for (const auto& child : item.dict) visit_value(child.second);
        auto marker = item.dict.find("__func__");
        auto cid = item.dict.find("__closure_id__");
        if (marker == item.dict.end() || marker->second.s != "func" || cid == item.dict.end()) return;
        const auto id = static_cast<size_t>(cid->second.n);
        if (!closures.insert(id).second) return;
        auto found = branch._closure_envs.find(id);
        if (found != branch._closure_envs.end()) {
            _closure_envs[id] = found->second;
            visit_env(found->second);
        }
    };
    visit_value(value);
}

Value Interpreter::exec_parallel(const std::vector<AxonNode*>& branches, EnvPtr env,
                                  bool pipeline_group, Value input) {
    if (branches.empty()) return pipeline_group ? input : Value::null();
    if (!pipeline_group) {
        for (const auto* branch : branches)
            if (!branch || (branch->type != AXON_NODE_ASSIGNMENT && branch->type != AXON_NODE_EXPR_STMT))
                throw RaiseSignal{"ParallelError: each branch must be an assignment or expression"};
    }
    struct Branch {
        std::unique_ptr<Interpreter> interpreter;
        EnvPtr env;
        Value result;
        std::exception_ptr error;
        std::ostringstream output, diagnostic;
    };
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    std::atomic<size_t> first_error{branches.size()};
    std::vector<std::unique_ptr<Branch>> states;
    for (size_t i = 0; i < branches.size(); ++i) {
        auto state = std::make_unique<Branch>();
        state->interpreter = fork_branch(env, state->env);
        state->interpreter->_execution = std::make_shared<ExecutionControl>(_execution->linked_cancel(cancelled));
        state->interpreter->_stdout = &state->output;
        state->interpreter->_stderr = &state->diagnostic;
        states.push_back(std::move(state));
    }
    // Each block has its own bounded pool. Nested blocks cannot deadlock waiting
    // for jobs in an exhausted parent pool; all pools join before state is merged.
    Scheduler scheduler({}, false);
    const size_t capacity = std::min<size_t>(8, std::max(2u, std::thread::hardware_concurrency()));
    scheduler.start(std::min(branches.size(), capacity));
    std::vector<std::future<void>> results;
    for (size_t i = 0; i < branches.size(); ++i) {
        results.push_back(scheduler.submit_work([&, i] {
            auto& state = *states[i];
            auto& worker = *state.interpreter;
            try {
                worker._execution->check();
                if (pipeline_group) {
                    state.env->set("__input__", input);
                    state.env->set("__pipeline_input__", input);
                    if (branches[i]->type == AXON_NODE_IDENTIFIER) {
                        auto agent = worker._agents.find(branches[i]->as.identifier.name);
                        if (agent != worker._agents.end()) {
                            state.result = worker.exec_agent(agent->second, input, state.env);
                            return;
                        }
                    }
                    if (branches[i]->type == AXON_NODE_TASK_CALL && branches[i]->as.task_call.args.count == 0)
                        state.env->set("flow.input", input);
                    state.result = worker.eval(branches[i], state.env);
                } else state.result = worker.exec_stmt(branches[i], state.env);
                worker._execution->check();
            } catch (...) {
                state.error = std::current_exception();
                size_t expected = branches.size();
                first_error.compare_exchange_strong(expected, i);
                cancelled->store(true, std::memory_order_relaxed);
            }
        }));
    }
    for (auto& result : results) result.get();
    scheduler.stop();
    for (auto& state : states) {
        (*_stdout) << state->output.str();
        (*_stderr) << state->diagnostic.str();
    }
    _execution->check();
    const size_t error = first_error.load();
    if (error < branches.size()) std::rethrow_exception(states[error]->error);
    // Commit only after all branches succeed. Bindings/task-cache collisions use
    // source order, retaining the previous last-declared-result convention.
    for (size_t i = 0; i < branches.size(); ++i) {
        auto& state = *states[i];
        import_branch_closures(*state.interpreter, state.result);
        if (!pipeline_group && branches[i]->type == AXON_NODE_ASSIGNMENT)
            env->assign(branches[i]->as.assignment.name, state.result);
        for (const auto& item : state.interpreter->_parallel_task_updates) {
            const auto separator = item.first.find('.');
            auto agent = _agents.find(item.first.substr(0, separator));
            if (agent != _agents.end() && separator != std::string::npos) {
                import_branch_closures(*state.interpreter, item.second);
                store_task_output(agent->second, item.first.substr(separator + 1), item.second);
            }
        }
    }
    return states.back()->result;
}

Value Interpreter::with_timeout(double seconds, const std::string& owner,
                                  const std::function<Value()>& action) {
    auto parent = _execution;
    _execution = std::make_shared<ExecutionControl>(parent->child(seconds, owner));
    ScopeExit restore([&] { _execution = parent; });
    try {
        _execution->check();
        Value result = action();
        _execution->check();
        return result;
    } catch (const ExecutionInterrupted& error) {
        // Only a local timeout can be recovered outside this budget. Parent
        // expiration and host cancellation must unwind the enclosing scope too.
        parent->check();
        _execution = parent;
        throw RaiseSignal{error.what()};
    }
}

Value Interpreter::run(AxonNode* program) {
    if (!program || program->type != AXON_NODE_PROGRAM)
        throw std::runtime_error("interpreter: expected PROGRAM node");

    _execution = std::make_shared<ExecutionControl>(_cancelled);
    try {
        _execution->check();
        // Mark the entry file too: a circular import must not collect it again.
        if (!_source_file.empty() && _source_file.front() != '<') {
            _imported_files.insert(std::filesystem::weakly_canonical(
                std::filesystem::absolute(_source_file)).generic_string());
        }

        // Pass 1: collect declarations
        collect(program, _global_env);

        // The selected flow's post-delay applies once, after successful execution.
        validate_delay(_flow_delay, "flow");
        return with_timeout(_flow_timeout, "flow", [&] {
            Value result = exec_flow(_global_env);
            sleep_checked(_flow_delay);
            return result;
        });
    } catch (const RaiseSignal& raised) {
        // Axon try/catch handles these signals inside the language. If one
        // escapes the program, expose a standard host error rather than abort.
        throw std::runtime_error("uncaught raise: " + raised.message);
    }
}

// ─────────────────────────────────────────────────────────────
// Pass 1 — collect agents, flows, graph
// ─────────────────────────────────────────────────────────────
void Interpreter::collect(AxonNode* prog, EnvPtr env) {
    for (size_t i = 0; i < prog->as.program.stmts.count; ++i) {
        AxonNode* s = prog->as.program.stmts.items[i];
        if (!s) continue;

        if (s->type == AXON_NODE_IMPORT) {
            std::string import_path = s->as.import_stmt.module ? s->as.import_stmt.module : "";
            log(AXON_LOG_INFO, "import: " + import_path);

            if (!import_path.empty() &&
                (import_path.find('/') != std::string::npos ||
                 import_path.find('\\') != std::string::npos ||
                 import_path.rfind(".axon") != std::string::npos)) {

                // Resolve all relative file imports against their importing file,
                // including "helper.axon" and nested ../ paths, never the caller's cwd.
                std::filesystem::path path(import_path);
                if (path.is_relative() && !_source_file.empty()) {
                    path = std::filesystem::path(_source_file).parent_path() / path;
                }
                std::string resolved = std::filesystem::weakly_canonical(
                    std::filesystem::absolute(path)).generic_string();

                // Guard against circular imports
                if (_imported_files.count(resolved)) {
                    log(AXON_LOG_WARN, "import: already imported " + resolved + " — skipping");
                } else {
                    _imported_files.insert(resolved);

                    // Read file
                    std::ifstream imp_f(resolved);
                    if (!imp_f) {
                        throw std::runtime_error("import: file not found: " + resolved);
                    }
                    std::ostringstream imp_ss; imp_ss << imp_f.rdbuf();
                    std::string imp_src = imp_ss.str();

                    // Parse using correct Axon API
                    Lexer imp_lex(imp_src, resolved);
                    auto imp_tokens = imp_lex.tokenize();
                    if (imp_lex.has_errors()) {
                        const auto& error = imp_lex.errors().front();
                        _imported_files.erase(resolved);
                        throw std::runtime_error("import: lex error in " + resolved +
                            ":" + std::to_string(error.line) + ": " + error.message);
                    }
                    AxonArena* imp_arena = axon_arena_new(256 * 1024);
                    _import_arenas.push_back(imp_arena);
                    Parser imp_par(std::move(imp_tokens), imp_arena);
                    AxonNode* imp_tree = imp_par.parse_program();
                    if (!imp_tree || imp_par.has_errors()) {
                        std::string detail;
                        if (imp_par.has_errors()) {
                            const auto& error = imp_par.errors().front();
                            detail = ":" + std::to_string(error.line) + ": " + error.message;
                        }
                        _imported_files.erase(resolved);
                        throw std::runtime_error("import: parse error in " + resolved + detail);
                    }

                    // Execute collect pass in a child env
                    // so private symbols (_name) don't leak to main program
                    std::string saved_source = _source_file;
                    bool        saved_debug  = _debug_mode;
                    _source_file = resolved;
                    EnvPtr imp_env = child_env(env);
                    try {
                        collect(imp_tree, imp_env);
                    } catch (...) {
                        _source_file = saved_source;
                        _debug_mode = saved_debug;
                        _imported_files.erase(resolved);
                        throw;
                    }
                    _source_file = saved_source;
                    _debug_mode  = saved_debug;

                    // Copy public symbols to main env
                    // Skip: private (_prefix), flow.* internals, __internals__
                    auto snap = imp_env->snapshot();
                    for (auto& [k, v] : snap) {
                        if (k.empty()) continue;
                        if (k[0] == '_') continue;                    // private
                        if (k.rfind("flow.", 0) == 0) continue;       // flow.*
                        if (k.rfind("__", 0) == 0) continue;          // __internal__
                        env->set(k, v);
                    }

                    log(AXON_LOG_INFO, "import: loaded " + resolved);
                    // Note: imp_arena kept alive (nodes referenced by _agents/_flows)
                }
            }

        } else if (s->type == AXON_NODE_FLOW_DEF) {
            // Store flow by name for multi-flow support
            std::string flow_name = s->as.flow_def.name ? s->as.flow_def.name : "";
            if (!flow_name.empty()) _flows[flow_name] = s;

            env->set("flow.version", Value::from(std::string("1.0")));
            if (!flow_name.empty()) env->set("flow.name", Value::from(flow_name));

            // Build a Dict for this flow so FlowName.key works as attr access
            Value flow_dict; flow_dict.kind = Value::Kind::Dict;
            flow_dict.dict["name"] = Value::from(flow_name);

            for (size_t j = 0; j < s->as.flow_def.entries.count; ++j) {
                auto& kv = s->as.flow_def.entries.items[j];
                Value v  = eval(kv.value, env);
                if (kv.key && std::string(kv.key) == "timeout" && v.kind != Value::Kind::Number)
                    throw std::runtime_error("flow: timeout must be a number");
                // Set flow.key (current active flow)
                env->set(std::string("flow.") + kv.key, v);
                // Add to flow dict for FlowName.key access
                if (kv.key) flow_dict.dict[kv.key] = v;
            }
            // Register flow dict by name: Extractor.input → Extractor["input"]
            if (!flow_name.empty()) env->set(flow_name, flow_dict);

            _out_cfg = s->as.flow_def.output_config;
            if (!_out_cfg.to_stdout && !_out_cfg.to_file)
                _out_cfg.to_stdout = 1;
            Value tel = env->get("flow.telemetry");
            if (tel.kind == Value::Kind::Bool) telemetry = tel.b;
            // flow delay and timeout
            Value fl_delay   = env->get("flow.delay");
            Value fl_timeout = env->get("flow.timeout");
            if (fl_delay.kind   == Value::Kind::Number) _flow_delay   = fl_delay.n;
            if (fl_timeout.kind == Value::Kind::Number) _flow_timeout = fl_timeout.n;

            // debug flag: controls verbose runtime messages
            Value dbg = env->get("flow.debug");
            if (dbg.kind == Value::Kind::Bool)
                _debug_mode = dbg.b;
            else
                _debug_mode = true; // default: show messages

        } else if (s->type == AXON_NODE_STORAGE_DEF) {
            std::unordered_map<std::string,std::string> cfg;
            for (size_t j = 0; j < s->as.storage_def.entries.count; ++j) {
                auto& kv = s->as.storage_def.entries.items[j];
                cfg[kv.key] = to_string(eval(kv.value, env));
            }
            _storages[s->as.storage_def.name] = cfg;
            env->set(s->as.storage_def.name,
                     Value::from(std::string(s->as.storage_def.name)));

        } else if (s->type == AXON_NODE_AGENT_DEF) {
            collect_agent(s, env);

        } else if (s->type == AXON_NODE_EMBEDDING_DEF) {
            // Configure semantic memory from embedding definition
            EmbedConfig cfg;
            for (size_t j = 0; j < s->as.embedding_def.entries.count; ++j) {
                auto& kv = s->as.embedding_def.entries.items[j];
                std::string val = to_string(eval(kv.value, env));
                std::string key = kv.key ? kv.key : "";
                if      (key == "provider") cfg.provider = val;
                else if (key == "model")    cfg.model    = val;
                else if (key == "endpoint") cfg.endpoint = val;
                else if (key == "key")      cfg.api_key  = val;
                else if (key == "dim")      cfg.dim      = std::stoi(val);
            }
            _shared->semantic = std::make_unique<SemanticMemory>(cfg);
            log(AXON_LOG_INFO, "embedding: " + std::string(s->as.embedding_def.name)
                + " [" + cfg.provider + "/" + cfg.model + "]");

        } else if (s->type == AXON_NODE_FUNC_DEF) {
            if (s->as.func_def.name) {
                size_t cid3 = _closure_id_counter->fetch_add(1, std::memory_order_relaxed);
                _closure_envs[cid3] = env;
                Value fn; fn.kind = Value::Kind::Dict;
                fn.dict["__func__"]       = Value::from(std::string("func"));
                fn.dict["__node__"]       = Value::from((double)(uintptr_t)s);
                fn.dict["__is_arrow__"]   = Value::from((bool)s->as.func_def.is_arrow);
                fn.dict["__closure_id__"] = Value::from((double)cid3);
                std::string fname = s->as.func_def.name;
                env->set(fname, fn);
                log(AXON_LOG_DEBUG, "func registered: " + fname);
            }

        } else if (s->type == AXON_NODE_GLOBAL_MAIN) {
            env->set("__global_main__",
                     Value::from(std::string("__has_global_main__")));
            _global_main_node = s;

        } else if (s->type == AXON_NODE_GRAPH_DEF) {
            _graph_node = s;
            // Build the GraphEngine
            for (size_t j = 0; j < s->as.graph_def.edges.count; ++j) {
                AxonNode* e = s->as.graph_def.edges.items[j];
                GraphEdge ge;
                ge.from        = {e->as.graph_edge.from};
                ge.to          = {e->as.graph_edge.to};
                ge.on_success  = e->as.graph_edge.attrs.on_success;
                ge.on_fail     = e->as.graph_edge.attrs.on_fail;
                ge.max_retries = e->as.graph_edge.attrs.max_retries;
                ge.condition   = e->as.graph_edge.attrs.condition
                                 ? e->as.graph_edge.attrs.condition : "";
                _graph->add_edge(ge);
            }
        }
    }
}


void Interpreter::collect_agent(AxonNode* node, EnvPtr env) {
    AgentDef def;
    def.name       = node->as.agent_def.name;
    def.provider   = node->as.agent_def.provider
                     ? node->as.agent_def.provider : "ollama";
    def.model      = node->as.agent_def.model
                     ? node->as.agent_def.model : "llama3";
    def.main_block = node->as.agent_def.main_block;
    def.task_graph = node->as.agent_def.task_graph;
    def.validation = node->as.agent_def.validation;

    // Evaluate temperature, max_tokens, num_ctx from extra kv pairs
    for (size_t i = 0; i < node->as.agent_def.extra.count; ++i) {
        auto& kv = node->as.agent_def.extra.items[i];
        if (!kv.key) continue;
        Value v = eval(kv.value, env);
        std::string key = kv.key;
        if (key == "temperature" && v.kind == Value::Kind::Number)
            def.temperature = v.n;
        else if (key == "max_tokens" && v.kind == Value::Kind::Number)
            def.max_tokens = (int)v.n;
        else if (key == "num_ctx" && v.kind == Value::Kind::Number)
            def.num_ctx = (int)v.n;
        else if (key == "num_gpu" && v.kind == Value::Kind::Number)
            def.num_gpu = (int)v.n;
        else if (key == "num_thread" && v.kind == Value::Kind::Number)
            def.num_thread = (int)v.n;
        else if (key == "main_gpu" && v.kind == Value::Kind::Number)
            def.main_gpu = (int)v.n;
        else if (key == "num_batch" && v.kind == Value::Kind::Number)
            def.num_batch = (int)v.n;
        else if (key == "f16_kv")
            def.f16_kv = (v.kind == Value::Kind::Bool) ? v.b : true;
        else if (key == "low_vram")
            def.low_vram = (v.kind == Value::Kind::Bool) ? v.b : false;
        else if (key == "gpu_fallback")
            def.gpu_fallback = (v.kind == Value::Kind::Bool) ? v.b : false;
        else if (key == "compute")
            def.compute = to_string(v);
        else if (key == "delay" && v.kind == Value::Kind::Number)
            def.delay = v.n;
        else if (key == "timeout") {
            if (v.kind != Value::Kind::Number)
                throw std::runtime_error("agent " + std::string(def.name) + ": timeout must be a number");
            def.timeout = v.n;
        }
    }

    // Index tasks by name AND preserve declaration order
    for (size_t i = 0; i < node->as.agent_def.tasks.count; ++i) {
        AxonNode* t = node->as.agent_def.tasks.items[i];
        std::string tname = t->as.task_def.name ? t->as.task_def.name : "";
        def.tasks[tname] = t;
        def.task_order.push_back(tname);  // preserve order
    }

    _agents[def.name] = std::move(def);
}

// ─────────────────────────────────────────────────────────────
// Pass 2 — execute flow  (Gap 1+4 fixed: on_success/on_fail/condition)
// ─────────────────────────────────────────────────────────────
Value Interpreter::exec_flow(EnvPtr env) {
    // If global main exists, execute it (main())
    if (_global_main_node) {
        log(AXON_LOG_INFO, "Executing global main");
        try {
            return exec_body(&_global_main_node->as.global_main.body, env);
        } catch (const ReturnSignal& r) { return r.value; }
    }

    if (_agents.empty()) {
        log(AXON_LOG_WARN, "No agents defined");
        return Value::null();
    }
    // Apply flow post-delay (after exec_global_main returns up the chain)

    Value flow_input = env->get("flow.input");

    // Find entry agent: first with no predecessors in the graph
    std::string start;
    for (auto& [name, _] : _agents) {
        if (_graph->node_count() == 0 ||
            _graph->predecessors({name}).empty()) {
            start = name; break;
        }
    }
    if (start.empty()) start = _agents.begin()->first;

    Value result = exec_graph_from(start, flow_input, env);

    if (_out_cfg.metrics) print_metrics();
    return result;
}

// ── Graph traversal with on_success/on_fail/condition (Gap 1+4) ──
Value Interpreter::exec_graph_from(const std::string& agent_name,
                                    Value input, EnvPtr env) {
    if (agent_name == "done" || agent_name == "fail") return input;

    auto it = _agents.find(agent_name);
    if (it == _agents.end()) return input;

    log(AXON_LOG_INFO, "Executing agent: " + agent_name);

    EnvPtr agent_env = child_env(env);
    agent_env->set("__input__", input);

    bool  success = true;
    bool  raised = false;
    Value output  = Value::null();

    try {
        output  = exec_agent(it->second, input, agent_env);
        success = !output.is_null();
    } catch (const RaiseSignal& r) {
        raised = true;
        success = false;
        output  = Value::from(r.message);
        log(AXON_LOG_WARN, "Agent '" + agent_name + "' raised: " + r.message);
    }

    // GAP 1+4: find and evaluate matching edges
    auto edges = _graph->edges_from({agent_name});

    if (edges.empty()) {
        if (raised) throw RaiseSignal{to_string(output)};
        return output;
    }

    for (auto& edge : edges) {
        bool follow = false;

        // Unconditional edge (no on_success, no on_fail, no condition)
        if (!edge.on_success && !edge.on_fail && edge.condition.empty()) {
            follow = true;
        }
        // on_success
        if (edge.on_success && success) follow = true;
        // on_fail
        if (edge.on_fail && !success) {
            // GAP 1: check max_retries for self-loops
            if (edge.to.value == agent_name) {
                int& retries = _retry_counts[agent_name];
                if (retries < edge.max_retries) {
                    ++retries;
                    log(AXON_LOG_INFO, "  retry " + std::to_string(retries)
                        + "/" + std::to_string(edge.max_retries));
                    return exec_graph_from(agent_name, input, env);
                }
                continue;
            }
            follow = true;
        }
        // GAP 4: condition expression evaluation
        if (!edge.condition.empty()) {
            follow = eval_condition(edge.condition, output, agent_env);
        }

        if (follow) {
            if (raised && (edge.to.value == "done" || edge.to.value == "fail") && !edge.on_fail)
                throw RaiseSignal{to_string(output)};
            if (edge.to.value == "done") return output;
            if (edge.to.value == "fail") {
                if (raised) throw RaiseSignal{to_string(output)};
                return Value::null();
            }
            return exec_graph_from(edge.to.value, output, env);
        }
    }

    // An unhandled failure must not become a successful CLI result.
    if (raised) throw RaiseSignal{to_string(output)};
    return output;
}

// ─────────────────────────────────────────────────────────────
// Execute one agent
// ─────────────────────────────────────────────────────────────
Value Interpreter::exec_agent(const AgentDef& def,
                               Value           input,
                               EnvPtr          flow_env) {
    return with_timeout(def.timeout, "agent " + std::string(def.name), [&]() -> Value {
    validate_delay(def.delay, "agent " + std::string(def.name));
    const AgentDef* previous = _current_agent;
    TaskFrame frame{&def, {}};
    _task_frames.push_back(&frame);
    _current_agent = &def;
    ScopeExit restore([&] {
        _task_frames.pop_back();
        _current_agent = previous;
    });
    EnvPtr env = child_env(flow_env);
    env->set("provider", Value::from(std::string(def.provider)));
    env->set("model", Value::from(std::string(def.model)));
    env->set("temperature", Value::from(def.temperature));
    env->set("__input__", input);
    for (const auto& [name, task] : def.tasks) env->set(name, Value::from(name));

    auto execute = [&]() -> Value {
        if (def.main_block) {
            try { return exec_main(def.main_block, env); }
            catch (const ReturnSignal& returned) { return returned.value; }
        }
        if (def.task_graph) {
            try { return exec_task_graph(def.task_graph, def, env); }
            catch (const ReturnSignal& returned) { return returned.value; }
        }
        Value current = input;
        for (const auto& name : def.task_order) {
            auto task = def.tasks.find(name);
            if (task != def.tasks.end()) {
                log(AXON_LOG_INFO, "  task: " + name);
                current = exec_task(def, name, task->second, env, current);
            }
        }
        return current;
    };
    Value result = execute();
    sleep_checked(def.delay);
    return result;
    });
}

const Value* Interpreter::find_task_output(const std::string& agent,
                                            const std::string& task) const {
    for (auto frame = _task_frames.rbegin(); frame != _task_frames.rend(); ++frame) {
        if (agent == (*frame)->agent->name) {
            auto found = (*frame)->outputs.find(task);
            return found == (*frame)->outputs.end() ? nullptr : &found->second;
        }
    }
    auto found = _task_outputs.find(agent + "." + task);
    return found == _task_outputs.end() ? nullptr : &found->second;
}

Value Interpreter::require_task_output(const std::string& agent,
                                        const std::string& task) const {
    const Value* output = find_task_output(agent, task);
    if (!output) throw std::runtime_error("task output not available: " + agent + "." + task);
    return *output;
}

void Interpreter::store_task_output(const AgentDef& def, const std::string& task,
                                     const Value& value) {
    _task_outputs[std::string(def.name) + "." + task] = value;
    _parallel_task_updates[std::string(def.name) + "." + task] = value;
    for (auto frame = _task_frames.rbegin(); frame != _task_frames.rend(); ++frame) {
        if ((*frame)->agent == &def) {
            (*frame)->outputs[task] = value;
            break;
        }
    }
}

Value Interpreter::resolve_task_input(const AgentDef& def, AxonNode* task,
                                       EnvPtr env, Value default_input) {
    for (size_t i = 0; i < task->as.task_def.entries.count; ++i) {
        const auto& entry = task->as.task_def.entries.items[i];
        if (!entry.key || std::string(entry.key) != "input") continue;
        AxonNode* expression = entry.value;
        // Agent->Task without arguments is an output reference in an input field.
        if (expression && expression->type == AXON_NODE_TASK_CALL &&
            expression->as.task_call.args.count == 0 &&
            expression->as.task_call.agent &&
            expression->as.task_call.agent->type == AXON_NODE_IDENTIFIER) {
            std::string name = expression->as.task_call.agent->as.identifier.name;
            auto agent = _agents.find(name);
            std::string task_name = expression->as.task_call.task;
            if (agent != _agents.end() && agent->second.tasks.count(task_name))
                return require_task_output(name, task_name);
        }
        // Explicit expressions, including __input__, flow.input and null,
        // are authoritative. Only an absent input field inherits the previous task.
        return eval(expression, env);
    }
    return default_input;
}

Value Interpreter::exec_task(const AgentDef& def, const std::string& name,
                              AxonNode* task, EnvPtr env, Value default_input,
                              bool explicit_input) {
    double timeout = 0;
    for (size_t i = 0; i < task->as.task_def.entries.count; ++i) {
        const auto& entry = task->as.task_def.entries.items[i];
        if (entry.key && std::string(entry.key) == "timeout") {
            Value value = eval(entry.value, env);
            if (value.kind != Value::Kind::Number)
                throw std::runtime_error("task " + name + ": timeout must be a number");
            timeout = value.n;
        }
    }
    return with_timeout(timeout, "task " + std::string(def.name) + "." + name, [&]() -> Value {
    Value input = explicit_input ? default_input
                                : resolve_task_input(def, task, env, default_input);
    EnvPtr task_env = child_env(env);
    task_env->set("__input__", input);
    std::vector<std::string> prompts;
    double delay = 0;
    const std::string saved_output = _expected_output;
    _expected_output.clear();
    ScopeExit restore_output([&] { _expected_output = saved_output; });
    for (size_t i = 0; i < task->as.task_def.entries.count; ++i) {
        const auto& entry = task->as.task_def.entries.items[i];
        if (!entry.key) continue;
        std::string key = entry.key;
        if (key == "prompt" || key == "prompts") {
            Value value = eval(entry.value, task_env);
            if (value.kind == Value::Kind::List)
                for (const auto& prompt : value.list) prompts.push_back(to_string(prompt));
            else prompts.push_back(to_string(value));
        } else if (key == "delay") {
            Value value = eval(entry.value, task_env);
            if (value.kind != Value::Kind::Number)
                throw std::runtime_error("task " + name + ": delay must be a number");
            delay = value.n;
        } else if (key == "output" && explicit_input) {
            // Preserve the legacy format handling for direct task calls only.
            // Sequential/task_graph execution previously ignored output= metadata.
            if (entry.value && entry.value->type == AXON_NODE_IDENTIFIER) {
                std::string format = entry.value->as.identifier.name;
                if (format == "json" || format == "text" || format == "csv" ||
                    format == "structured" || format == "minimal")
                    _expected_output = format;
                else _expected_output = to_string(eval(entry.value, task_env));
            } else _expected_output = to_string(eval(entry.value, task_env));
        }
    }
    validate_delay(delay, "task " + name);
    Value result = prompts.empty() ? input : call_llm(def, prompts, input, task_env);
    sleep_checked(delay);
    store_task_output(def, name, result);
    env->set(name, result);
    return result;
    });
}

// ─────────────────────────────────────────────────────────────
// Execute main block
// ─────────────────────────────────────────────────────────────
Value Interpreter::exec_main(AxonNode* main, EnvPtr env) {
    return exec_body(&main->as.main_block.body, env);
}

// ─────────────────────────────────────────────────────────────
// Execute task graph within an agent
// ─────────────────────────────────────────────────────────────
Value Interpreter::exec_task_graph(AxonNode*       tg,
                                   const AgentDef& def,
                                   EnvPtr          env) {
    // Build a local graph for task execution
    GraphEngine local_graph;
    for (size_t i = 0; i < tg->as.task_graph.edges.count; ++i) {
        AxonNode* e = tg->as.task_graph.edges.items[i];
        GraphEdge ge;
        ge.from       = {e->as.task_edge.from};
        ge.to         = {e->as.task_edge.to};
        ge.on_success = e->as.task_edge.attrs.on_success;
        ge.on_fail    = e->as.task_edge.attrs.on_fail;
        local_graph.add_edge(ge);
    }

    // Topological execution
    std::vector<AgentID> order;
    try {
        order = local_graph.topological_order();
    } catch (...) {
        if (!def.tasks.empty())
            order.push_back({def.tasks.begin()->first});
    }

    Value last = env->get("__input__");

    for (auto& tid : order) {
        if (tid.value == "done") break;

        auto it = def.tasks.find(tid.value);
        if (it == def.tasks.end()) continue;

        log("  task: " + tid.value);
        AxonNode* t_node = it->second;

        last = exec_task(def, tid.value, t_node, env, last);
    }

    return last;
}

// ─────────────────────────────────────────────────────────────
// Execute body (list of statements)
// ─────────────────────────────────────────────────────────────
Value Interpreter::exec_body(AxonNodeList* body, EnvPtr env) {
    Value last = Value::null();
    for (size_t i = 0; i < body->count; ++i) {
        last = exec_stmt(body->items[i], env);
    }
    return last;
}

// ─────────────────────────────────────────────────────────────
// Execute statement
// ─────────────────────────────────────────────────────────────
Value Interpreter::exec_stmt(AxonNode* node, EnvPtr env) {
    _execution->check();
    if (!node) return Value::null();

    // Shell interop: direct dispatch (bypasses known dispatch gap)
    if (node->type == AXON_NODE_INTEROP_BLOCK
        && node->as.interop_block.lang == AXON_INTEROP_SHELL) {
        return exec_interop(node, env);
    }

    switch (node->type) {

        case AXON_NODE_ASSIGNMENT: {
            Value v = eval(node->as.assignment.value, env);
            env->assign(node->as.assignment.name, v);
            return v;
        }

        case AXON_NODE_EXPR_STMT:
            return eval(node->as.expr_stmt.expr, env);

        case AXON_NODE_IF_STMT:
            return exec_if(node, env);

        case AXON_NODE_FOR_STMT:
            return exec_for(node, env);

        case AXON_NODE_WHILE_STMT:
            return exec_while(node, env);

        case AXON_NODE_MATCH_STMT:
            return exec_match(node, env);

        case AXON_NODE_TRY_STMT:
            return exec_try(node, env);

        case AXON_NODE_RAISE_STMT: {
            Value v = eval(node->as.raise_stmt.value, env);
            throw RaiseSignal{to_string(v)};
        }

        case AXON_NODE_RETURN_STMT: {
            Value v = node->as.return_stmt.value
                      ? eval(node->as.return_stmt.value, env)
                      : Value::null();
            throw ReturnSignal{v};
        }

        case AXON_NODE_FUNC_DEF:
        case AXON_NODE_ANON_FUNC: {
            // Create function value with captured env (closure)
            size_t cid = _closure_id_counter->fetch_add(1, std::memory_order_relaxed);
            _closure_envs[cid] = env;   // capture current scope
            Value fn; fn.kind = Value::Kind::Dict;
            fn.dict["__func__"]     = Value::from(std::string("func"));
            fn.dict["__node__"]     = Value::from((double)(uintptr_t)node);
            fn.dict["__is_arrow__"] = Value::from((bool)node->as.func_def.is_arrow);
            fn.dict["__closure_id__"] = Value::from((double)cid);
            if (node->as.func_def.name) {
                std::string fname = node->as.func_def.name;
                env->set(fname, fn);
            }
            return fn;
        }

        case AXON_NODE_INTEROP_BLOCK:
            return exec_interop(node, env);

        case AXON_NODE_INDEX_ASSIGN:
            return exec_index_assign(node, env);

        case AXON_NODE_ATTR_ASSIGN:
            return exec_attr_assign(node, env);

        case AXON_NODE_RUN_STMT: {
            std::string target = node->as.run_stmt.target
                                 ? node->as.run_stmt.target : "";
            log(AXON_LOG_DEBUG, "run " + target);
            if (_current_agent && target == "task_graph"
                && _current_agent->task_graph) {
                try {
                    return exec_task_graph(
                        _current_agent->task_graph, *_current_agent, env);
                } catch (const ReturnSignal& r) { return r.value; }
            }
            return Value::null();
        }

        case AXON_NODE_PARALLEL_BLOCK: {
            std::vector<AxonNode*> branches;
            for (size_t i = 0; i < node->as.parallel_block.assignments.count; ++i)
                branches.push_back(node->as.parallel_block.assignments.items[i]);
            return exec_parallel(branches, env);
        }

        case AXON_NODE_SPAWN_STMT: {
            std::string aname = node->as.spawn_stmt.agent_name
                                ? node->as.spawn_stmt.agent_name : "";
            Value input = node->as.spawn_stmt.input
                          ? eval(node->as.spawn_stmt.input, env)
                          : env->get("__input__");
            log(AXON_LOG_INFO, "spawn " + aname + " (async fire-and-forget)");
            // Phase 2B: run synchronously; true async in Phase 3
            auto it = _agents.find(aname);
            if (it != _agents.end()) {
                EnvPtr spawn_env = child_env(env);
                spawn_env->set("__input__", input);
                exec_agent(it->second, input, spawn_env);
            }
            return Value::null();
        }

        default:
            return Value::null();
    }
}

Value Interpreter::exec_if(AxonNode* node, EnvPtr env) {
    // Use parent env directly (Python-style): variables assigned inside
    // if/else if/else are visible in the enclosing scope.
    Value cond = eval(node->as.if_stmt.condition, env);
    if (truthy(cond)) {
        return exec_body(&node->as.if_stmt.then_body, env);
    } else if (node->as.if_stmt.else_body.count > 0) {
        return exec_body(&node->as.if_stmt.else_body, env);
    }
    return Value::null();
}

Value Interpreter::exec_for(AxonNode* node, EnvPtr env) {
    // Share parent scope — variables assigned in body are visible outside
    Value iter = eval(node->as.for_stmt.iter, env);
    Value last = Value::null();
    if (iter.kind != Value::Kind::List) return Value::null();
    for (auto& item : iter.list) {
        _execution->check();
        env->set(node->as.for_stmt.var, item);  // loop var in parent scope
        try {
            last = exec_body(&node->as.for_stmt.body, env);
        } catch (const ReturnSignal&) { throw; }
    }
    return last;
}

Value Interpreter::exec_while(AxonNode* node, EnvPtr env) {
    // Share parent scope so variables assigned inside are visible outside
    Value last = Value::null();
    while (truthy(eval(node->as.while_stmt.condition, env))) {
        try {
            last = exec_body(&node->as.while_stmt.body, env);
        } catch (const ReturnSignal&) { throw; }
    }
    return last;
}

Value Interpreter::exec_match(AxonNode* node, EnvPtr env) {
    Value subject = eval(node->as.match_stmt.subject, env);
    std::string subj_str = to_string(subject);

    for (size_t i = 0; i < node->as.match_stmt.arms.count; ++i) {
        AxonNode* arm = node->as.match_stmt.arms.items[i];
        AxonNode* pat = arm->as.match_arm.pattern;

        bool matched = false;
        if (pat->type == AXON_NODE_IDENTIFIER
            && strcmp(pat->as.identifier.name, "_") == 0) {
            matched = true;  // wildcard
        } else {
            Value pv = eval(pat, env);
            matched = (to_string(pv) == subj_str);
        }

        if (matched) {
            EnvPtr child = child_env(env);
            AxonNode* body = arm->as.match_arm.body;
            if (body->type == AXON_NODE_MAIN_BLOCK)
                return exec_body(&body->as.main_block.body, child);
            if (body->type == AXON_NODE_RETURN_STMT) {
                Value v = body->as.return_stmt.value
                          ? eval(body->as.return_stmt.value, child)
                          : Value::null();
                throw ReturnSignal{v};
            }
            return eval(body, child);
        }
    }
    return Value::null();
}

Value Interpreter::exec_try(AxonNode* node, EnvPtr env) {
    try {
        EnvPtr child = child_env(env);
        return exec_body(&node->as.try_stmt.try_body, child);
    } catch (const RaiseSignal& r) {
        EnvPtr child = child_env(env);
        child->set(node->as.try_stmt.catch_var, Value::from(r.message));
        return exec_body(&node->as.try_stmt.catch_body, child);
    }
}

Value Interpreter::exec_interop(AxonNode* node, EnvPtr env) {
    std::unique_lock<std::recursive_timed_mutex> native_guard;
    if (node->as.interop_block.lang != AXON_INTEROP_SQL) native_guard = native_lock();
    std::string code = node->as.interop_block.code
                       ? node->as.interop_block.code : "";

    if (node->as.interop_block.lang == AXON_INTEROP_PYTHON) {
        // Write temp Python file, inject Axon variables, run subprocess,
        // capture __result__ via a sentinel prefix in stdout.
        TemporaryDirectory temporary;
        std::string tmp_path = (temporary.path / "script.py").string();
        // Strip common leading whitespace from code (dedent)
        std::string dedented;
        {
            std::istringstream ss(code);
            std::string ln;
            // Find minimum indent
            size_t min_indent = std::string::npos;
            std::vector<std::string> raw_lines;
            while (std::getline(ss, ln)) {
                raw_lines.push_back(ln);
                if (ln.find_first_not_of(" 	") != std::string::npos) {
                    size_t ind = ln.find_first_not_of(" 	");
                    if (ind < min_indent) min_indent = ind;
                }
            }
            if (min_indent == std::string::npos) min_indent = 0;
            for (auto& l : raw_lines) {
                if (l.size() >= min_indent)
                    dedented += l.substr(min_indent) + "\n";
                else
                    dedented += l + "\n";
            }
        }
        std::string script;

        // Inject Axon scope variables as Python globals
        auto snap = env->snapshot();
        for (auto& kv : snap) {
            const std::string& k = kv.first;
            if (k.empty() || k[0] == '_') continue;
            std::string safe = k;
            for (char& c : safe) if (c == '.') c = '_';
            const Value& v = kv.second;
            if (k == "print" && v.kind == Value::Kind::String && v.s == "__builtin_print__") continue;
            if (v.kind == Value::Kind::String) {
                // JSON string literals are valid Python string literals too;
                // preserve newlines/control characters from document/OCR context.
                script += safe + " = " + json_stringify(v, 0) + "\n";
            } else if (v.kind == Value::Kind::Number) {
                script += safe + " = " + to_string(v) + "\n";
            } else if (v.kind == Value::Kind::Bool) {
                script += safe + " = " + (v.b ? "True" : "False") + "\n";
            }
        }

        script += "\n";
        script += dedented;
        script += "\n";
        // Capture __result__ with a sentinel line
        script += "import sys as _sys, json as _json\n";
        script += "try:\n";
        script += "    _r = __result__\n";
        script += "    print('__COG__' + _json.dumps(_r))\n";
        script += "except NameError:\n";
        script += "    pass\n";

        // Write file
        {
            FILE* f = fopen(tmp_path.c_str(), "w");
            if (f) { fputs(script.c_str(), f); fclose(f); }
        }

        // Run subprocess
#ifdef AXON_OS_WINDOWS
        std::string py_cmd = "python \"" + tmp_path + "\" 2>nul";
#else
        std::string py_cmd = "python3 \"" + tmp_path + "\" 2>/dev/null";
#endif
#ifdef AXON_OS_WINDOWS
        FILE* pipe = _popen(py_cmd.c_str(), "r");
#else
        FILE* pipe = popen(py_cmd.c_str(), "r");
#endif
        std::string output;
        if (pipe) {
            char buf[512];
            while (fgets(buf, sizeof(buf), pipe)) output += buf;
#ifdef AXON_OS_WINDOWS
            _pclose(pipe);
#else
            pclose(pipe);
#endif
        }
        remove(tmp_path.c_str());

        // Parse output: print normal lines, extract __result__
        Value py_result = Value::null();
        {
            std::istringstream iss(output);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.size() >= 7 &&
                    line.substr(0, 7) == "__COG__") {
                    std::string jv = line.substr(7);
                    // Unwrap JSON string literal
                    if (!jv.empty() && jv.front() == '"') {
                        // strip outer quotes and unescape
                        jv = jv.substr(1, jv.size() - 2);
                        std::string unesc;
                        for (size_t i = 0; i < jv.size(); ++i) {
                            if (jv[i] == '\\' && i + 1 < jv.size()) {
                                char nx = jv[++i];
                                if (nx == 'n') unesc += '\n';
                                else if (nx == 't') unesc += '\t';
                                else if (nx == '"') unesc += '"';
                                else if (nx == '\\') unesc += '\\';
                                else { unesc += '\\'; unesc += nx; }
                            } else { unesc += jv[i]; }
                        }
                        jv = unesc;
                    }
                    py_result = Value::from(jv);
                    env->set("__result__", py_result);
                    env->set("python.__result__", py_result);
                    // Also set python as a dict so python.__result__ works via attr_access
                    Value py_obj; py_obj.kind = Value::Kind::Dict;
                    py_obj.dict["__result__"] = py_result;
                    env->set("python", py_obj);
                } else if (!line.empty()) {
                    (*_stdout) << line << "\n";
                }
            }
        }
        return py_result;
    }

    if (node->as.interop_block.lang == AXON_INTEROP_SHELL) {
        // Execute and capture stdout
        std::string sh_out;
        int rc = 0;
#ifdef AXON_OS_WINDOWS
        FILE* p = _popen((code + " 2>&1").c_str(), "r");
#else
        FILE* p = popen((code + " 2>&1").c_str(), "r");
#endif
        if (p) {
            char buf[256];
            while (fgets(buf, sizeof(buf), p)) sh_out += buf;
#ifdef AXON_OS_WINDOWS
            rc = _pclose(p);
#else
            rc = pclose(p);
#endif
        }
        if (!sh_out.empty()) (*_stdout) << sh_out;
        Value stdout_val = Value::from(sh_out);
        Value rc_val     = Value::from((double)rc);
        env->set("shell.stdout",   stdout_val);
        env->set("shell.exitcode", rc_val);
        env->set("__result__",     stdout_val);
        // shell as dict: shell.stdout, shell.exitcode
        Value sh_obj; sh_obj.kind = Value::Kind::Dict;
        sh_obj.dict["stdout"]   = stdout_val;
        sh_obj.dict["exitcode"] = rc_val;
        sh_obj.dict["__result__"] = stdout_val;
        env->set("shell", sh_obj);
        return stdout_val;
    }

    if (node->as.interop_block.lang == AXON_INTEROP_SQL) {
        Value connection = env->get("__sql_connection__");
        if (connection.is_null()) throw RaiseSignal{"SqlError: select a connection with sql->use(db) before a sql block"};
        Value results = eval_builtin_call("sql", "script", {connection, Value::from(code)}, env);
        Value result = results.list.back().dict.at("rows");
        Value object; object.kind = Value::Kind::Dict;
        object.dict["result"] = result;
        object.dict["__result__"] = result;
        object.dict["results"] = results;
        object.dict["connection"] = connection;
        env->set("sql", object);
        env->set("sql.result", result);
        env->set("sql.__result__", result);
        env->set("__result__", result);
        return result;
    }

    return Value::null();
}

// ─────────────────────────────────────────────────────────────
// LLM call
// ─────────────────────────────────────────────────────────────
Value Interpreter::call_llm(const AgentDef&                   def,
                             const std::vector<std::string>&   prompts,
                             const Value&                      input,
                             EnvPtr                            env) {
    auto it = _providers.find(def.provider);
    _execution->check();
    if (it == _providers.end()) {
        const std::string message = "ProviderError: provider '" + std::string(def.provider)
            + "' is not registered";
        _global_env->set("__last_llm_error__", Value::from(message));
        throw RaiseSignal{message};
    }

    // Build message list
    std::vector<ConversationTurn> messages;
    for (const auto& p : prompts)
        messages.push_back({"system", p, 0});

    // Add user input
    std::string input_str = to_string(input);
    if (!input_str.empty() && input_str != "null")
        messages.push_back({"user", input_str, 0});

    LLMRequest req;
    req.provider    = def.provider;
    req.model       = def.model;
    req.messages    = std::move(messages);
    req.temperature = def.temperature;
    req.max_tokens  = def.max_tokens;
    if (def.num_ctx    > 0)  req.num_ctx    = def.num_ctx;
    req.num_gpu     = def.num_gpu;
    if (def.num_thread > 0)  req.num_thread = def.num_thread;
    req.main_gpu    = def.main_gpu;
    if (def.num_batch  > 0)  req.num_batch  = def.num_batch;
    req.f16_kv       = def.f16_kv;
    req.low_vram     = def.low_vram;
    req.gpu_fallback = def.gpu_fallback;
    req.compute      = def.compute;

    // compute = "auto": detect GPU and set num_gpu accordingly
    if (def.compute == "auto") {
        req.num_gpu = -1;          // try GPU first
        req.gpu_fallback = true;   // auto-fallback if GPU fails
    } else if (def.compute == "cpu") {
        req.num_gpu = 0;
        req.num_thread = (def.num_thread > 0) ? def.num_thread : 4;
    } else if (def.compute == "gpu") {
        req.num_gpu = -1;
    }

    log("  → " + std::string(def.provider) + "/" + def.model);

    try {
        req.control = _execution;
        _global_env->set("__last_llm_error__", Value::null());
        std::unique_lock<std::timed_mutex> provider_guard(*_provider_locks.at(def.provider), std::defer_lock);
        if (!it->second->supports_concurrent_requests()) {
            while (!provider_guard.try_lock_for(std::chrono::milliseconds(10))) _execution->check();
        }
        _execution->check();
        LLMResponse resp = it->second->complete(req);
        _execution->check();

        log("  ← " + std::to_string(resp.tokens_out) + " tokens "
            + "(" + std::to_string((int)resp.latency_ms) + "ms)");

        std::string raw = resp.content;

        // GAP 3: if task expects JSON, try to parse it
        if (!_expected_output.empty() && _expected_output == "json") {
            std::string json_str = extract_json(raw);
            if (!json_str.empty()) {
                Value parsed = parse_json_value(json_str);
                if (!parsed.is_null()) return parsed;
            }
            log(AXON_LOG_WARN, "  output=json but response could not be parsed as JSON");
        }

        return Value::from(raw);

    } catch (const ExecutionInterrupted&) {
        throw;
    } catch (const std::exception& e) {
        const std::string message = "ProviderError: " + std::string(def.provider)
            + "/" + def.model + ": " + e.what();
        _global_env->set("__last_llm_error__", Value::from(message));
        throw RaiseSignal{message};
    }
}

// ─────────────────────────────────────────────────────────────
// Expression evaluation
// ─────────────────────────────────────────────────────────────
Value Interpreter::eval(AxonNode* node, EnvPtr env) {
    _execution->check();
    if (!node) return Value::null();

    switch (node->type) {

        case AXON_NODE_NUMBER:
            return Value::from(node->as.number.value);

        case AXON_NODE_STRING:
            return Value::from(std::string(node->as.string.value));

        case AXON_NODE_BOOL:
            return Value::from((bool)node->as.boolean.value);

        case AXON_NODE_NULL:
            return Value::null();

        case AXON_NODE_IDENTIFIER: {
            std::string name = node->as.identifier.name;
            // Special built-ins
            if (name == "__input__")  return env->get("__input__");
            if (name == "__result__") return env->get("__result__");
            Value v = env->get(name);
            if (v.is_null() && name != "null") {
                // Check if this name was explicitly assigned in any scope
                // by checking if it exists in env (not just null value)
                // We do this by checking if env has an entry for it
                bool in_env = env->contains(name);
                if (!in_env) {
                    static const std::vector<std::string> builtins = {
                        "null","true","false","print","typeof",
                        "shell","python","sql","flow","_"
                    };
                    bool is_builtin = false;
                    for (auto& b : builtins)
                        if (b == name) { is_builtin = true; break; }
                    if (!is_builtin && name.find('.') == std::string::npos) {
                        auto ait = _agents.find(name);
                        if (ait == _agents.end()) {
                            runtime_error(
                                "undefined variable '" + name + "'",
                                node->loc.line);
                        }
                    }
                }
            }
            if (v.is_null()) {
                // Only error if the variable was NEVER assigned
                if (!env->contains(name)) {
                    static const std::vector<std::string> safe = {
                        "null","true","false","print","typeof",
                        "shell","python","sql","flow","_",
                        "text","json","csv","structured","minimal"
                    };
                    bool is_safe = false;
                    for (auto& s : safe)
                        if (s == name) { is_safe = true; break; }
                    if (!is_safe
                        && name.find('.') == std::string::npos
                        && _agents.find(name) == _agents.end()
                        && _global_env->get(name).is_null()) {
                        runtime_error(
                            "undefined variable '" + name + "'",
                            node->loc.line);
                    }
                }
            }

            return v;
        }

        case AXON_NODE_LIST: {
            Value v; v.kind = Value::Kind::List;
            for (size_t i = 0; i < node->as.list.elements.count; ++i)
                v.list.push_back(eval(node->as.list.elements.items[i], env));
            return v;
        }

        case AXON_NODE_DICT: {
            Value v; v.kind = Value::Kind::Dict;
            for (size_t i = 0; i < node->as.dict.pairs.count; ++i) {
                AxonNode* p = node->as.dict.pairs.items[i];
                v.dict[p->as.dict_pair.key] =
                    eval(p->as.dict_pair.value, env);
            }
            return v;
        }

        case AXON_NODE_BINARY_EXPR:  return eval_binary(node, env);
        case AXON_NODE_UNARY_EXPR:   return eval_unary(node, env);
        case AXON_NODE_ATTR_ACCESS:  return eval_attr(node, env);
        case AXON_NODE_METHOD_CALL:  return eval_method(node, env);
        case AXON_NODE_FUNC_CALL:    return eval_call(node, env);
        case AXON_NODE_INDEX_ACCESS: return eval_index(node, env);

        case AXON_NODE_TYPEOF: {
            Value v = eval(node->as.typeof_expr.expr, env);
            switch (v.kind) {
                case Value::Kind::String:  return Value::from(std::string("string"));
                case Value::Kind::Number:  return Value::from(std::string("number"));
                case Value::Kind::Bool:    return Value::from(std::string("bool"));
                case Value::Kind::List:    return Value::from(std::string("list"));
                case Value::Kind::Dict:    return Value::from(std::string("dict"));
                default:                   return Value::from(std::string("null"));
            }
        }
        case AXON_NODE_FSTRING: {
            // Evaluate f"hello {expr} world {expr2}"
            std::string raw  = node->as.fstring.raw ? node->as.fstring.raw : "";
            std::string result;
            size_t i = 0;
            while (i < raw.size()) {
                if (raw[i] == '{') {
                    // Find matching closing }
                    size_t j = i + 1;
                    int depth = 1;
                    while (j < raw.size() && depth > 0) {
                        if (raw[j] == '{') ++depth;
                        if (raw[j] == '}') --depth;
                        ++j;
                    }
                    std::string expr_str = raw.substr(i+1, j-i-2);
                    // Evaluate the expression
                    Lexer     expr_lex(expr_str, "<fstring>");
                    auto      expr_toks = expr_lex.tokenize();
                    AxonArena* expr_arena = axon_arena_new(4096);
                    Parser    expr_p(std::move(expr_toks), expr_arena);
                    AxonNode* expr_node = expr_p.parse_expression();
                    Value     expr_val  = (!expr_p.has_errors() && expr_node)
                                         ? eval(expr_node, env) : Value::null();
                    axon_arena_free(expr_arena);
                    result += to_string(expr_val);
                    i = j;
                } else if (raw[i] == '\\' && i+1 < raw.size()) {
                    char esc = raw[i+1];
                    if (esc == 'n') result += '\n';
                    else if (esc == 't') result += '\t';
                    else result += esc;
                    i += 2;
                } else {
                    result += raw[i++];
                }
            }
            return Value::from(result);
        }

        case AXON_NODE_SAFE_ACCESS: {
            try { return eval(node->as.safe_access.obj, env); }
            catch (...) { return Value::null(); }
        }

        case AXON_NODE_TASK_CALL:  return eval_task_call(node, env);
        case AXON_NODE_PIPELINE:   return eval_pipeline(node, env);
        case AXON_NODE_FUNC_DEF:
        case AXON_NODE_ANON_FUNC: {
            size_t cid2 = _closure_id_counter->fetch_add(1, std::memory_order_relaxed);
            _closure_envs[cid2] = env;
            Value fn; fn.kind = Value::Kind::Dict;
            fn.dict["__func__"]       = Value::from(std::string("func"));
            fn.dict["__node__"]       = Value::from((double)(uintptr_t)node);
            fn.dict["__is_arrow__"]   = Value::from((bool)node->as.func_def.is_arrow);
            fn.dict["__closure_id__"] = Value::from((double)cid2);
            if (node->as.func_def.name) env->set(node->as.func_def.name, fn);
            return fn;
        }

        // Statements used as expressions in match arms
        case AXON_NODE_RETURN_STMT:
            if (node->as.return_stmt.value)
                return eval(node->as.return_stmt.value, env);
            return Value::null();

        default:
            return exec_stmt(node, env);
    }
}

Value Interpreter::eval_binary(AxonNode* node, EnvPtr env) {
    Value L = eval(node->as.binary_expr.left,  env);
    Value R = eval(node->as.binary_expr.right, env);

    switch (node->as.binary_expr.op) {
        case AXON_OP_ADD:
            if (L.kind == Value::Kind::String ||
                R.kind == Value::Kind::String)
                return Value::from(to_string(L) + to_string(R));
            return Value::from(L.n + R.n);
        case AXON_OP_SUB: return Value::from(L.n - R.n);
        case AXON_OP_MUL:
            if (L.kind==Value::Kind::Number && R.kind==Value::Kind::Number)
                return Value::from(L.n * R.n);
            // string * number: repeat (Python-style)
            if (L.kind==Value::Kind::String && R.kind==Value::Kind::Number) {
                int n=(int)R.n; if(n<=0) return Value::from(std::string(""));
                std::string r; for(int i=0;i<n;++i) r+=L.s;
                return Value::from(r);
            }
            if (L.kind==Value::Kind::Number && R.kind==Value::Kind::String) {
                int n=(int)L.n; if(n<=0) return Value::from(std::string(""));
                std::string r; for(int i=0;i<n;++i) r+=R.s;
                return Value::from(r);
            }
            return Value::from(0.0);
        case AXON_OP_DIV:
            if (R.n == 0) throw RaiseSignal{"DivisionByZero"};
            return Value::from(L.n / R.n);
        case AXON_OP_MOD: return Value::from(std::fmod(L.n, R.n));
        case AXON_OP_POW: return Value::from(std::pow(L.n, R.n));
        case AXON_OP_EQ:  return Value::from(to_string(L) == to_string(R));
        case AXON_OP_NEQ: return Value::from(to_string(L) != to_string(R));
        case AXON_OP_LT:  return Value::from(L.n  < R.n);
        case AXON_OP_GT:  return Value::from(L.n  > R.n);
        case AXON_OP_LTE: return Value::from(L.n <= R.n);
        case AXON_OP_GTE: return Value::from(L.n >= R.n);
        case AXON_OP_AND: return Value::from(truthy(L) && truthy(R));
        case AXON_OP_OR:  return Value::from(truthy(L) || truthy(R));
        default:          return Value::null();
    }
}

Value Interpreter::eval_unary(AxonNode* node, EnvPtr env) {
    Value v = eval(node->as.unary_expr.operand, env);
    if (node->as.unary_expr.op == AXON_OP_NEG)
        return Value::from(-v.n);
    if (node->as.unary_expr.op == AXON_OP_NOT)
        return Value::from(!truthy(v));
    return Value::null();
}

Value Interpreter::attr_of(const Value& obj, const std::string& attr) {
    if (obj.kind == Value::Kind::Dict) {
        auto it = obj.dict.find(attr);
        return it != obj.dict.end() ? it->second : Value::null();
    }
    return Value::null();
}

Value Interpreter::eval_attr(AxonNode* node, EnvPtr env) {
    if (std::string(node->as.attr_access.attr) == "output") {
        AxonNode* object = node->as.attr_access.obj;
        if (object && object->type == AXON_NODE_IDENTIFIER && _current_agent) {
            std::string task = object->as.identifier.name;
            if (_current_agent->tasks.count(task))
                return require_task_output(_current_agent->name, task);
        }
        if (object && object->type == AXON_NODE_ATTR_ACCESS &&
            object->as.attr_access.obj->type == AXON_NODE_IDENTIFIER) {
            std::string agent = object->as.attr_access.obj->as.identifier.name;
            std::string task = object->as.attr_access.attr;
            auto found = _agents.find(agent);
            if (found != _agents.end() && found->second.tasks.count(task))
                return require_task_output(agent, task);
        }
    }
    if (node->as.attr_access.obj->type == AXON_NODE_IDENTIFIER) {
        std::string agent = node->as.attr_access.obj->as.identifier.name;
        std::string task = node->as.attr_access.attr;
        auto found = _agents.find(agent);
        if (found != _agents.end() && found->second.tasks.count(task))
            return require_task_output(agent, task);
    }
    Value obj = eval(node->as.attr_access.obj, env);
    std::string attr = node->as.attr_access.attr;

    // Try compound key first: python.__result__, shell.stdout, sql.__result__
    if (node->as.attr_access.obj->type == AXON_NODE_IDENTIFIER) {
        std::string obj_name = node->as.attr_access.obj->as.identifier.name;
        std::string compound = obj_name + "." + attr;

        // Task output exposed from a previous Agent.run(): AgentName.TaskName
        auto tit = _task_outputs.find(compound);
        if (tit != _task_outputs.end()) return tit->second;

        Value v = env->get(compound);
        if (!v.is_null()) return v;
    }
    // Also try __result__ directly
    if (attr == "__result__") {
        Value v = env->get("__result__");
        if (!v.is_null()) return v;
    }

    return attr_of(obj, attr);
}

Value Interpreter::eval_method(AxonNode* node, EnvPtr env) {
    Value      obj    = eval(node->as.method_call.obj, env);
    std::string method = node->as.method_call.method;
    // Evaluate args once for all method calls
    std::vector<Value> args;
    for (size_t i = 0; i < node->as.method_call.args.count; ++i)
        args.push_back(eval(node->as.method_call.args.items[i], env));

    // ── List methods ──────────────────────────────────────────
    if (obj.kind == Value::Kind::List) {
        if (method == "append") {
            Value new_item = args.empty() ? Value::null() : args[0];
            // Mutate in env if obj is a variable
            if (node->as.method_call.obj->type == AXON_NODE_IDENTIFIER) {
                std::string var = node->as.method_call.obj->as.identifier.name;
                Value lst = env->get(var);
                if (lst.kind == Value::Kind::List) {
                    lst.list.push_back(new_item);
                    env->assign(var, lst);
                    return lst;
                }
            }
            Value lst = obj; lst.list.push_back(new_item); return lst;
        }
        if (method == "pop") {
            if (node->as.method_call.obj->type == AXON_NODE_IDENTIFIER) {
                std::string var = node->as.method_call.obj->as.identifier.name;
                Value lst = env->get(var);
                if (lst.kind == Value::Kind::List && !lst.list.empty()) {
                    Value popped = lst.list.back();
                    lst.list.pop_back();
                    env->assign(var, lst);
                    return popped;
                }
            }
            return Value::null();
        }
        if (method == "length" || method == "size" || method == "len")
            return Value::from((double)obj.list.size());
        if (method == "contains" || method == "includes") {
            if (args.empty()) return Value::from(false);
            std::string target = to_string(args[0]);
            for (auto& item : obj.list)
                if (to_string(item) == target) return Value::from(true);
            return Value::from(false);
        }
        if (method == "join") {
            std::string sep = args.empty() ? "" : to_string(args[0]);
            std::string result;
            for (size_t i = 0; i < obj.list.size(); ++i) {
                if (i) result += sep;
                result += to_string(obj.list[i]);
            }
            return Value::from(result);
        }
        if (method == "first") return obj.list.empty() ? Value::null() : obj.list.front();
        if (method == "last")  return obj.list.empty() ? Value::null() : obj.list.back();
        if (method == "reverse") {
            Value r = obj;
            std::reverse(r.list.begin(), r.list.end());
            return r;
        }
        if (method == "isempty" || method == "empty")
            return Value::from(obj.list.empty());
    }

    // ── String methods ────────────────────────────────────────
    if (obj.kind == Value::Kind::String) {
        const std::string& s = obj.s;

        if (method == "find" || method == "index") {
            if (args.empty()) return Value::from(-1.0);
            size_t pos = s.find(to_string(args[0]));
            return Value::from(pos == std::string::npos ? -1.0 : (double)pos);
        }
        if (method == "contains" || method == "includes") {
            if (args.empty()) return Value::from(false);
            return Value::from(s.find(to_string(args[0])) != std::string::npos);
        }
        if (method == "startswith") {
            if (args.empty()) return Value::from(false);
            std::string pref = to_string(args[0]);
            return Value::from(s.size() >= pref.size()
                               && s.substr(0, pref.size()) == pref);
        }
        if (method == "endswith") {
            if (args.empty()) return Value::from(false);
            std::string suf = to_string(args[0]);
            return Value::from(s.size() >= suf.size()
                               && s.substr(s.size()-suf.size()) == suf);
        }
        if (method == "strip" || method == "trim") {
            size_t a = s.find_first_not_of(" \t\n\r");
            if (a == std::string::npos) return Value::from(std::string(""));
            size_t b = s.find_last_not_of(" \t\n\r");
            return Value::from(s.substr(a, b-a+1));
        }
        if (method == "lstrip") {
            size_t a = s.find_first_not_of(" \t\n\r");
            return Value::from(a == std::string::npos ? std::string("") : s.substr(a));
        }
        if (method == "rstrip") {
            size_t b = s.find_last_not_of(" \t\n\r");
            return Value::from(b == std::string::npos ? std::string("") : s.substr(0, b+1));
        }

        if (method == "upper") {
            std::string r = s;
            for (char& c : r) c = (char)toupper(c);
            return Value::from(r);
        }
        if (method == "lower") {
            std::string r = s;
            for (char& c : r) c = (char)tolower(c);
            return Value::from(r);
        }
        if (method == "isempty" || method == "empty")
            return Value::from(s.empty());
        if (method == "length" || method == "len" || method == "size")
            return Value::from((double)s.size());
        if (method == "repeat") {
            int n = args.empty() ? 1 : (int)args[0].n;
            std::string r; for (int i=0;i<n;++i) r+=s;
            return Value::from(r);
        }
        if (method == "toint") {
            try { return Value::from((double)std::stoi(s)); }
            catch (...) { return Value::null(); }
        }
        if (method == "tofloat") {
            try { return Value::from(std::stod(s)); }
            catch (...) { return Value::null(); }
        }

        // ── Regex methods ─────────────────────────────────────
        // str.find(pattern)  -> first match string or null
        if (method == "find" || method == "match") {
            if (args.empty()) return Value::null();
            try {
                std::regex re(to_string(args[0]));
                std::smatch m;
                if (std::regex_search(s, m, re))
                    return Value::from(m[0].str());
            } catch (...) {}
            return Value::null();
        }
        // str.find_all(pattern)  -> List of all matches
        if (method == "find_all") {
            if (args.empty()) { Value r; r.kind=Value::Kind::List; return r; }
            try {
                std::regex re(to_string(args[0]));
                std::sregex_iterator it(s.begin(), s.end(), re);
                std::sregex_iterator end;
                Value r; r.kind = Value::Kind::List;
                for (; it != end; ++it)
                    r.list.push_back(Value::from((*it)[0].str()));
                return r;
            } catch (...) { Value r; r.kind=Value::Kind::List; return r; }
        }
        // str.replace(pattern, replacement) — regex or literal
        if (method == "replace") {
            if (args.size() < 2) return obj;
            std::string pattern = to_string(args[0]);
            std::string repl    = to_string(args[1]);
            try {
                std::regex re(pattern);
                return Value::from(std::regex_replace(s, re, repl));
            } catch (...) {
                // Fallback to literal replace
                std::string result = s;
                size_t pos = 0;
                while ((pos = result.find(pattern, pos)) != std::string::npos) {
                    result.replace(pos, pattern.size(), repl);
                    pos += repl.size();
                }
                return Value::from(result);
            }
        }
        // str.split(pattern)  — regex or literal
        if (method == "split") {
            std::string sep = args.empty() ? " " : to_string(args[0]);
            Value result; result.kind = Value::Kind::List;
            try {
                std::regex re(sep);
                std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
                std::sregex_token_iterator end;
                for (; it != end; ++it)
                    result.list.push_back(Value::from(it->str()));
                return result;
            } catch (...) {
                // Fallback literal split
                size_t start = 0, pos;
                while ((pos = s.find(sep, start)) != std::string::npos) {
                    result.list.push_back(Value::from(s.substr(start, pos-start)));
                    start = pos + sep.size();
                }
                result.list.push_back(Value::from(s.substr(start)));
                return result;
            }
        }
        // str.test(pattern)   -> bool
        if (method == "test" || method == "matches") {
            if (args.empty()) return Value::from(false);
            try {
                std::regex re(to_string(args[0]));
                return Value::from(std::regex_search(s, re));
            } catch (...) { return Value::from(false); }
        }
        // str.sub(pattern, repl, count=1) -> first N replacements
        if (method == "sub") {
            if (args.size() < 2) return obj;
            try {
                std::regex re(to_string(args[0]));
                std::string repl = to_string(args[1]);
                int count = args.size() >= 3 ? (int)args[2].n : 1;
                std::string result;
                std::sregex_iterator it(s.begin(), s.end(), re);
                std::sregex_iterator end;
                size_t last = 0;
                for (; it != end && count > 0; ++it, --count) {
                    result += s.substr(last, it->position() - last);
                    result += repl;
                    last = it->position() + it->length();
                }
                result += s.substr(last);
                return Value::from(result);
            } catch (...) { return obj; }
        }
    }

    // ── Number methods ────────────────────────────────────────
    if (obj.kind == Value::Kind::Number) {
        if (method == "tostr" || method == "str")
            return Value::from(to_string(obj));
        if (method == "abs")
            return Value::from(std::abs(obj.n));
        if (method == "floor")
            return Value::from(std::floor(obj.n));
        if (method == "ceil")
            return Value::from(std::ceil(obj.n));
        if (method == "round")
            return Value::from(std::round(obj.n));
    }

    // Agent.run(input) — execute an agent from global main
    if (method == "run") {
        // Get agent name from obj node (IDENTIFIER or string value)
        std::string agent_name;
        if (node->as.method_call.obj &&
            node->as.method_call.obj->type == AXON_NODE_IDENTIFIER)
            agent_name = node->as.method_call.obj->as.identifier.name;
        if (agent_name.empty()) agent_name = to_string(obj);
        auto it = _agents.find(agent_name);
        if (it != _agents.end()) {
            Value input = args.empty() ? env->get("flow.input") : args[0];
            EnvPtr agent_env = child_env(env);
            agent_env->set("__input__", input);
            try {
                return exec_agent(it->second, input, agent_env);
            } catch (const ReturnSignal& r) { return r.value; }
        }
        log(AXON_LOG_WARN, "Agent not found: " + agent_name);
        return Value::null();
    }

    // Built-in methods
    if (method == "isempty") {
        bool empty = obj.is_null()
                  || (obj.kind == Value::Kind::String && obj.s.empty())
                  || (obj.kind == Value::Kind::List   && obj.list.empty())
                  || (obj.kind == Value::Kind::Dict   && obj.dict.empty());
        return Value::from(empty);
    }
    if (method == "length" || method == "size") {
        if (obj.kind == Value::Kind::String)
            return Value::from((double)obj.s.size());
        if (obj.kind == Value::Kind::List)
            return Value::from((double)obj.list.size());
        return Value::from(0.0);
    }
    if (method == "upper") {
        std::string s = to_string(obj);
        for (char& c : s) c = (char)toupper(c);
        return Value::from(s);
    }
    if (method == "lower") {
        std::string s = to_string(obj);
        for (char& c : s) c = (char)tolower(c);
        return Value::from(s);
    }
    if (method == "current") {
        // session.current()
        return Value::from(std::string("session-default"));
    }
    if (method == "new") {
        auto sid = _sessions->create();
        return Value::from(sid.value);
    }

    return Value::null();
}

Value Interpreter::eval_call(AxonNode* node, EnvPtr env) {
    Value callee = eval(node->as.func_call.callee, env);

    // print(...)
    if (callee.kind == Value::Kind::String && callee.s == "sleep") {
        std::vector<Value> sargs;
        for (size_t i = 0; i < node->as.func_call.args.count; ++i)
            sargs.push_back(eval(node->as.func_call.args.items[i], env));
        double secs = sargs.empty() ? 0.0 : sargs[0].n;
        if (secs > 0) sleep_checked(secs);
        return Value::null();
    }
        if (callee.kind == Value::Kind::String && callee.s == "__builtin_print__") {
        for (size_t i = 0; i < node->as.func_call.args.count; ++i) {
            if (i) (*_stdout) << "";
            Value pv = eval(node->as.func_call.args.items[i], env);
            // Use JSON stringify for Dict/List so output is readable
            if (pv.kind == Value::Kind::Dict || pv.kind == Value::Kind::List)
                (*_stdout) << json_stringify(pv, 0);
            else
                (*_stdout) << to_string(pv);
        }
        (*_stdout) << "\n";
        return Value::null();
    }

    // typeof(val) -> "string" | "number" | "bool" | "list" | "dict" | "null"
    if (callee.kind == Value::Kind::String && callee.s == "__builtin_typeof__") {
        Value v = node->as.func_call.args.count > 0
                  ? eval(node->as.func_call.args.items[0], env) : Value::null();
        switch (v.kind) {
            case Value::Kind::String:  return Value::from(std::string("string"));
            case Value::Kind::Number:  return Value::from(std::string("number"));
            case Value::Kind::Bool:    return Value::from(std::string("bool"));
            case Value::Kind::List:    return Value::from(std::string("list"));
            case Value::Kind::Dict:    return Value::from(std::string("dict"));
            default:                   return Value::from(std::string("null"));
        }
    }

    // ── Call user-defined func ───────────────────────────────
    if (callee.kind == Value::Kind::Dict &&
        callee.dict.count("__func__") &&
        callee.dict["__func__"].s == "func") {
        auto it_n = callee.dict.find("__node__");
        if (it_n != callee.dict.end()) {
            AxonNode* fn_node = (AxonNode*)(uintptr_t)(it_n->second.n);
            if (fn_node) {
                const size_t expected = fn_node->as.func_def.params.count;
                const size_t actual = node->as.func_call.args.count;
                if (actual != expected) {
                    const std::string label = fn_node->as.func_def.name
                        ? "function '" + std::string(fn_node->as.func_def.name) + "'"
                        : "anonymous function";
                    // Reject before evaluating arguments or entering the body.
                    throw RaiseSignal{"ArityError: " + label + " expects "
                        + std::to_string(expected) + " argument(s), got "
                        + std::to_string(actual) + " at line "
                        + std::to_string(node->loc.line)};
                }
                // Use captured closure env, fall back to caller env
                EnvPtr base_env = env;
                auto it_cid = callee.dict.find("__closure_id__");
                if (it_cid != callee.dict.end()) {
                    size_t cid = (size_t)it_cid->second.n;
                    auto it_ce = _closure_envs.find(cid);
                    if (it_ce != _closure_envs.end()) base_env = it_ce->second;
                }
                EnvPtr fn_env = child_env(base_env);

                // Bind positional arguments to parameter names
                for (size_t i = 0; i < fn_node->as.func_def.params.count; ++i) {
                    AxonNode* param = fn_node->as.func_def.params.items[i];
                    std::string pname = param && param->as.identifier.name
                                        ? param->as.identifier.name : "";
                    Value arg = eval(node->as.func_call.args.items[i], env);
                    fn_env->set(pname, arg);
                }

                // Execute body
                try {
                    if (fn_node->as.func_def.is_arrow) {
                        return eval(fn_node->as.func_def.body, fn_env);
                    } else if (fn_node->as.func_def.body) {
                        return exec_body(
                            &fn_node->as.func_def.body->as.main_block.body,
                            fn_env);
                    }
                } catch (const ReturnSignal& r) {
                    return r.value;
                }
            }
        }
    }

    // Agent(input) — direct call without ->
    if (node->as.func_call.callee &&
        node->as.func_call.callee->type == AXON_NODE_IDENTIFIER) {
        std::string aname = node->as.func_call.callee->as.identifier.name;
        auto it = _agents.find(aname);
        if (it != _agents.end()) {
            std::vector<Value> cargs;
            for (size_t i = 0; i < node->as.func_call.args.count; ++i)
                cargs.push_back(eval(node->as.func_call.args.items[i], env));
            Value inp = cargs.empty() ? env->get("flow.input") : cargs[0];
            EnvPtr aenv = child_env(env);
            aenv->set("__input__", inp);
            try { return exec_agent(it->second, inp, aenv); }
            catch (const ReturnSignal& r) { return r.value; }
        }
    }

    return Value::null();
}


Value Interpreter::eval_index(AxonNode* node, EnvPtr env) {
    Value obj   = eval(node->as.index_access.obj,   env);
    Value index = eval(node->as.index_access.index, env);

    try {
        // String index: s[i] → single character (supports negative indices)
        if (obj.kind == Value::Kind::String) {
            int idx = (int)index.n;
            int len = (int)obj.s.size();
            if (idx < 0) idx = len + idx;
            if (idx >= 0 && idx < len)
                return Value::from(std::string(1, obj.s[(size_t)idx]));
            return Value::null();
        }
        if (obj.kind == Value::Kind::List) {
            int idx = (int)index.n;
            int len = (int)obj.list.size();
            if (idx < 0) idx = len + idx;
            if (idx >= 0 && idx < len) return obj.list[(size_t)idx];
        }
        if (obj.kind == Value::Kind::Dict) {
            auto it = obj.dict.find(to_string(index));
            if (it != obj.dict.end()) return it->second;
        }
    } catch (...) {}

    if (node->as.index_access.safe) return Value::null();
    return Value::null();
}


// ── Source tracking for error reporting ─────────────────────
std::string Interpreter::val_to_str(const Value& v) {
    if (v.kind == Value::Kind::String) return v.s;
    if (v.kind == Value::Kind::Number) {
        if (v.n == (long long)v.n) return std::to_string((long long)v.n);
        return std::to_string(v.n);
    }
    if (v.kind == Value::Kind::Bool) return v.b ? "true" : "false";
    return "";
}

void Interpreter::set_source(const std::string& path, const std::string& src) {
    _source_file = path;
    _source_lines.clear();
    std::istringstream ss(src);
    std::string line;
    while (std::getline(ss, line)) _source_lines.push_back(line);
}

void Interpreter::runtime_error(const std::string& msg, int line) {
    (*_stderr) << "\n\033[31m[runtime error]\033[0m " << msg << "\n";
    if (line > 0 && !_source_file.empty()) {
        (*_stderr) << "  --> " << _source_file << ":" << line << "\n";
        if (line <= (int)_source_lines.size()) {
            std::string& src_line = _source_lines[line - 1];
            (*_stderr) << "   " << line << " | " << src_line << "\n";
        }
    }
    (*_stderr) << "\n";
    throw std::runtime_error(msg);
}

// ── exec_index_assign: obj[key] = value ──────────────────────
// Helper: set a value deep inside a nested dict/list structure
// path = list of keys to follow, value to set at the end
static bool deep_set(Value& root, const std::vector<Value>& path,
                     size_t depth, const Value& val) {
    if (depth == path.size() - 1) {
        // At the target level
        const Value& k = path[depth];
        if (root.kind == Value::Kind::Dict) {
            root.dict[axon::Interpreter::val_to_str(k)] = val;
            return true;
        }
        if (root.kind == Value::Kind::List) {
            size_t idx = (size_t)k.n;
            while (root.list.size() <= idx) root.list.push_back(Value::null());
            root.list[idx] = val;
            return true;
        }
        // Auto-create dict
        root.kind = Value::Kind::Dict;
        root.dict[axon::Interpreter::val_to_str(k)] = val;
        return true;
    }
    // Recurse deeper
    const Value& k = path[depth];
    if (root.kind == Value::Kind::Dict) {
        std::string ks = axon::Interpreter::val_to_str(k);
        if (root.dict.find(ks) == root.dict.end())
            root.dict[ks].kind = Value::Kind::Dict; // auto-create
        return deep_set(root.dict[ks], path, depth + 1, val);
    }
    if (root.kind == Value::Kind::List) {
        size_t idx = (size_t)k.n;
        while (root.list.size() <= idx) root.list.push_back(Value::null());
        return deep_set(root.list[idx], path, depth + 1, val);
    }
    return false;
}

Value Interpreter::exec_index_assign(AxonNode* node, EnvPtr env) {
    Value val = eval(node->as.index_assign.value, env);

    // Build path of keys by walking the nested index access chain
    std::vector<Value> path;

    // Collect keys bottom-up, then reverse
    AxonNode* obj = node->as.index_assign.obj;
    path.push_back(eval(node->as.index_assign.key, env));

    while (obj && obj->type == AXON_NODE_INDEX_ACCESS) {
        path.push_back(eval(obj->as.index_access.index, env));
        obj = obj->as.index_access.obj;
    }
    std::reverse(path.begin(), path.end());

    // obj should now be IDENTIFIER (the root variable)
    if (!obj || obj->type != AXON_NODE_IDENTIFIER) {
        log(AXON_LOG_WARN, "index assign: root must be a variable");
        return Value::null();
    }

    const char* var_name = obj->as.identifier.name;
    Value root = env->get(var_name);
    if (root.is_null()) {
        root.kind = Value::Kind::Dict; // auto-create
    }

    deep_set(root, path, 0, val);
    env->assign(var_name, root);
    return val;
}

// ── exec_attr_assign: obj.attr = value ───────────────────────
Value Interpreter::exec_attr_assign(AxonNode* node, EnvPtr env) {
    Value value = eval(node->as.attr_assign.value, env);
    AxonNode* obj_node = node->as.attr_assign.obj;
    const char* attr   = node->as.attr_assign.attr;

    if (!obj_node || obj_node->type != AXON_NODE_IDENTIFIER || !attr)
        return Value::null();

    const char* var_name = obj_node->as.identifier.name;
    Value container = env->get(var_name);

    if (container.kind == Value::Kind::Dict || container.is_null()) {
        if (container.is_null()) container.kind = Value::Kind::Dict;
        container.dict[attr] = value;
        env->assign(var_name, container);
        return value;
    }
    return Value::null();
}

// ── eval_task_call: Agent->Task(args) ────────────────────────
Value Interpreter::eval_task_call(AxonNode* node, EnvPtr env) {
    // Evaluate args
    std::vector<Value> args;
    for (size_t i = 0; i < node->as.task_call.args.count; ++i)
        args.push_back(eval(node->as.task_call.args.items[i], env));

    Value input = args.empty() ? env->get("flow.input") : args[0];

    // Determine if it's a builtin module or an agent
    std::string agent_name;
    if (node->as.task_call.agent &&
        node->as.task_call.agent->type == AXON_NODE_IDENTIFIER)
        agent_name = node->as.task_call.agent->as.identifier.name;

    std::string task_name = node->as.task_call.task
                            ? node->as.task_call.task : "";

    // Builtin modules: file, memory, storage, whisper
    static const std::vector<std::string> builtins = {
        "file", "memory", "storage", "whisper", "shell",
        "csv", "json", "date", "pdf", "image", "codec", "hash", "ocr", "sleep", "print", "type", "http", "sql"
    };
    for (auto& b : builtins) {
        if (agent_name == b)
            return eval_builtin_call(agent_name, task_name, args, env);
    }

    // Agent task execution
    auto it = _agents.find(agent_name);
    if (it == _agents.end()) {
        log(AXON_LOG_WARN, "Agent not found: " + agent_name);
        return Value::null();
    }

    const AgentDef& def = it->second;

    if (!task_name.empty()) {
        if (args.empty()) {
            if (const Value* cached = find_task_output(agent_name, task_name)) return *cached;
        }
        auto task = def.tasks.find(task_name);
        if (task == def.tasks.end())
            throw std::runtime_error("task not found: " + agent_name + "->" + task_name);
        const AgentDef* previous = _current_agent;
        TaskFrame frame{&def, {}};
        _task_frames.push_back(&frame);
        _current_agent = &def;
        ScopeExit restore([&] {
            _task_frames.pop_back();
            _current_agent = previous;
        });
        EnvPtr task_env = child_env(env);
        task_env->set("__input__", input);
        for (const auto& item : def.tasks) task_env->set(item.first, Value::from(item.first));
        return with_timeout(def.timeout, "agent " + agent_name, [&] {
            return exec_task(def, task_name, task->second, task_env, input, true);
        });
    }

    // Run full agent
    EnvPtr agent_env = child_env(env);
    agent_env->set("__input__", input);
    try {
        return exec_agent(def, input, agent_env);
    } catch (const ReturnSignal& r) { return r.value; }
}

// ── eval_pipeline: input >> Stage >> [A,B] >> C ──────────────
Value Interpreter::eval_pipeline(AxonNode* node, EnvPtr env) {
    Value current_val = eval(node->as.pipeline.input, env);

    if (node->as.pipeline.parallel) {
        std::vector<AxonNode*> branches;
        for (size_t i = 0; i < node->as.pipeline.stages.count; ++i)
            branches.push_back(node->as.pipeline.stages.items[i]);
        return exec_parallel(branches, env, true, current_val);
    } else if (node->as.pipeline.stages.count == 1) {
        AxonNode* stage = node->as.pipeline.stages.items[0];
        EnvPtr stage_env = child_env(env);
        stage_env->set("__input__", current_val);
        if (stage->type == AXON_NODE_IDENTIFIER) {
            std::string aname = stage->as.identifier.name;
            auto it = _agents.find(aname);
            if (it != _agents.end()) {
                try {
                    return exec_agent(it->second, current_val, stage_env);
                } catch (const ReturnSignal& r) { return r.value; }
            }
        }
        if (stage->type == AXON_NODE_TASK_CALL) {
            // Agent->Task in pipeline: inject current_val as first arg if no args
            if (stage->as.task_call.args.count == 0) {
                stage_env->set("flow.input", current_val);
            }
            return eval_task_call(stage, stage_env);
        }
        return eval(stage, stage_env);
    }
    return current_val;
}

// ── eval_builtin_call: file->, memory->, storage->, whisper-> ──
Value Interpreter::eval_builtin_call(const std::string&         mod,
                                      const std::string&         fn,
                                      const std::vector<Value>&  args,
                                      EnvPtr                     env) {
    if (mod == "sql" || (mod == "storage" && fn == "query")) {
        try {
            if (mod == "sql" && fn == "use") {
                if (args.size() != 1) throw std::runtime_error("SqlError: use requires one connection");
                Value open = _shared->sql.call("is_open", args, _execution, reinterpret_cast<uintptr_t>(this));
                if (!open.b) throw std::runtime_error("SqlError: connection is closed");
                env->set("__sql_connection__", args[0]);
                return args[0];
            }
            return _shared->sql.call(fn, args, _execution, reinterpret_cast<uintptr_t>(this));
        } catch (const ExecutionInterrupted& error) {
            _execution->check();
            throw RaiseSignal{error.what()};
        } catch (const std::exception& error) {
            throw RaiseSignal{error.what()};
        }
    }
    if (mod == "http") {
        try { return http_module_call(fn, args, _execution); }
        catch (const ExecutionInterrupted& error) {
            _execution->check(); // A parent deadline/cancellation cannot be suppressed.
            throw RaiseSignal{error.what()};
        } catch (const std::exception& error) {
            throw RaiseSignal{error.what()};
        }
    }
    auto native_guard = native_lock();

    // ── file->read(path, raw=false) ──────────────────────────
    if (mod == "file" && fn == "read") {
        if (args.empty()) return Value::null();
        std::string path = to_string(args[0]);
        // raw=true forces returning raw text regardless of extension
        bool force_raw = args.size() >= 2 && args[1].b == true;

        std::ifstream f(path, std::ios::binary);
        if (!f) {
            log(AXON_LOG_WARN, "file->read: cannot open " + path);
            return Value::null();
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::string raw_text = ss.str();

        if (!force_raw) {
            FileType ft = detect_file_type(path);
            if (ft == FileType::CSV) {
                // Auto-detect delimiter (tab for .tsv)
                char delim = (path.size() >= 4 &&
                              path.substr(path.size()-4) == ".tsv") ? '\t' : ',';
                log(AXON_LOG_INFO, "file->read: " + path + " [CSV, "
                    + std::to_string(raw_text.size()) + " bytes]");
                return csv_parse(raw_text, delim, true);
            }
            if (ft == FileType::JSON) {
                log(AXON_LOG_INFO, "file->read: " + path + " [JSON, "
                    + std::to_string(raw_text.size()) + " bytes]");
                return json_parse(raw_text);
            }
            if (ft == FileType::PDF) {
                log(AXON_LOG_INFO, "file->read: " + path + " [PDF]");
                return read_pdf_file(raw_text);
            }
            if (ft == FileType::DOCX) {
                log(AXON_LOG_INFO, "file->read: " + path + " [DOCX]");
                return read_docx_file(raw_text);
            }
            if (ft == FileType::XLSX) {
                log(AXON_LOG_INFO, "file->read: " + path + " [XLSX]");
                return read_xlsx_file(raw_text);
            }
        }

        log(AXON_LOG_INFO, "file->read: " + path
            + " (" + std::to_string(raw_text.size()) + " bytes)");
        return Value::from(raw_text);
    }

    // ── file->write(path, content) ───────────────────────────
    if (mod == "file" && fn == "write") {
        if (args.size() < 2) return Value::null();
        std::string path    = to_string(args[0]);
        std::string content = to_string(args[1]);
        std::ofstream f(path);
        if (!f) {
            log(AXON_LOG_WARN, "file->write: cannot write " + path);
            return Value::from(false);
        }
        f << content;
        log(AXON_LOG_INFO, "file->write: " + path
            + " (" + std::to_string(content.size()) + " bytes)");
        return Value::from(true);
    }

    // ── csv->parse(text, delimiter=",") ─────────────────────
    if (mod == "csv" && fn == "parse") {
        if (args.empty()) return Value::null();
        char delim = args.size() >= 2 ? to_string(args[1])[0] : ',';
        bool header = args.size() >= 3 ? args[2].b : true;
        Value r = csv_parse(to_string(args[0]), delim, header);
        log(AXON_LOG_INFO, "csv->parse: " + std::to_string(r.list.size()) + " rows");
        return r;
    }

    // ── csv->stringify(data, delimiter=",") ──────────────────
    if (mod == "csv" && fn == "stringify") {
        if (args.empty()) return Value::from(std::string(""));
        char delim = args.size() >= 2 ? to_string(args[1])[0] : ',';
        return Value::from(csv_stringify(args[0], delim));
    }

    // ── csv->write(path, data) ────────────────────────────────
    if (mod == "csv" && fn == "write") {
        if (args.size() < 2) return Value::from(false);
        std::string path = to_string(args[0]);
        std::string text = csv_stringify(args[1]);
        std::ofstream f(path);
        if (!f) return Value::from(false);
        f << text;
        log(AXON_LOG_INFO, "csv->write: " + path);
        return Value::from(true);
    }

    // ── json->parse(text) ─────────────────────────────────────
    if (mod == "json" && fn == "parse") {
        if (args.empty()) return Value::null();
        // If already a parsed Value (Dict/List), return as-is
        if (args[0].kind == Value::Kind::Dict ||
            args[0].kind == Value::Kind::List)
            return args[0];
        // Parse from string
        Value r = json_parse(to_string(args[0]));
        return r;
    }

    // ── json->stringify(value, indent=0) ──────────────────────
    if (mod == "json" && fn == "stringify") {
        if (args.empty()) return Value::from(std::string("{}"));
        int indent = args.size() >= 2 ? (int)args[1].n : 0;
        return Value::from(json_stringify(args[0], indent));
    }

    // ── json->write(path, value, indent=2) ────────────────────
    if (mod == "json" && fn == "write") {
        if (args.size() < 2) return Value::from(false);
        std::string path = to_string(args[0]);
        int indent = args.size() >= 3 ? (int)args[2].n : 2;
        std::string text = json_stringify(args[1], indent);
        std::ofstream f(path);
        if (!f) return Value::from(false);
        f << text << "\n";
        log(AXON_LOG_INFO, "json->write: " + path);
        return Value::from(true);
    }

    // ── file->delete(path) ──────────────────────────────────
    if (mod == "file" && (fn == "delete" || fn == "remove")) {
        if (args.empty()) return Value::from(false);
        std::string path = to_string(args[0]);
        return Value::from(std::remove(path.c_str()) == 0);
    }

    // ── file->mkdir(path) ────────────────────────────────────
    if (mod == "file" && fn == "mkdir") {
        if (args.empty()) return Value::from(false);
        std::string path = to_string(args[0]);
#ifdef AXON_OS_WINDOWS
        int rc = _mkdir(path.c_str());
#else
        int rc = mkdir(path.c_str(), 0755);
#endif
        return Value::from(rc == 0);
    }

    // ── file->rmdir(path) ────────────────────────────────────
    if (mod == "file" && fn == "rmdir") {
        if (args.empty()) return Value::from(false);
        std::string path = to_string(args[0]);
#ifdef AXON_OS_WINDOWS
        int rc = _rmdir(path.c_str());
#else
        int rc = rmdir(path.c_str());
#endif
        return Value::from(rc == 0);
    }

    // ── file->ls(path) — list directory entries ──────────────
    if (mod == "file" && fn == "ls") {
        std::string path = args.empty() ? "." : to_string(args[0]);
        Value result; result.kind = Value::Kind::List;
#ifdef AXON_OS_WINDOWS
        WIN32_FIND_DATA fd;
        HANDLE h = FindFirstFile((path+"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do { result.list.push_back(Value::from(std::string(fd.cFileName))); }
            while (FindNextFile(h, &fd));
            FindClose(h);
        }
#else
        struct dirent* entry;
        DIR* dir = opendir(path.c_str());
        if (dir) {
            while ((entry = readdir(dir)))
                result.list.push_back(Value::from(std::string(entry->d_name)));
            closedir(dir);
        }
#endif
        return result;
    }

    // ── file->append(path, content) ──────────────────────────
    if (mod == "file" && fn == "append") {
        if (args.size() < 2) return Value::null();
        std::string path    = to_string(args[0]);
        std::string content = to_string(args[1]);
        std::ofstream f(path, std::ios::app);
        if (!f) return Value::from(false);
        f << content;
        return Value::from(true);
    }

    // ── file->exists(path) ───────────────────────────────────
    if (mod == "file" && fn == "exists") {
        if (args.empty()) return Value::from(false);
        std::ifstream f(to_string(args[0]));
        return Value::from(f.good());
    }

    // ── shell->exec(cmd) — Phase 2D shell interop ────────────
    if (mod == "shell" && fn == "exec") {
        if (args.empty()) return Value::null();
        std::string cmd = to_string(args[0]);
        std::string output;
        int rc = 0;
#ifdef AXON_OS_WINDOWS
        FILE* p = _popen((cmd + " 2>&1").c_str(), "r");
#else
        FILE* p = popen((cmd + " 2>&1").c_str(), "r");
#endif
        if (p) {
            char buf[256];
            while (fgets(buf, sizeof(buf), p)) output += buf;
#ifdef AXON_OS_WINDOWS
            rc = _pclose(p);
#else
            rc = pclose(p);
#endif
        }
        Value out_val = Value::from(output);
        Value rc_val  = Value::from((double)rc);
        env->set("shell.stdout",   out_val);
        env->set("shell.exitcode", rc_val);
        env->set("__result__",     out_val);
        Value sh_obj; sh_obj.kind = Value::Kind::Dict;
        sh_obj.dict["stdout"]   = out_val;
        sh_obj.dict["exitcode"] = rc_val;
        env->set("shell", sh_obj);
        return out_val;
    }

    // ── memory->store(key, value, scope="runtime", ttl=0) ───────
    if (mod == "memory" && fn == "store") {
        if (args.size() < 2) return Value::from(false);
        std::string key   = to_string(args[0]);
        std::string value = to_string(args[1]);
        MemScope scope = MemScope::Runtime;
        uint32_t ttl   = 0;
        if (args.size() >= 3) {
            std::string sc = to_string(args[2]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
        }
        if (args.size() >= 4) ttl = (uint32_t)args[3].n;
        _shared->memory.store(key, value, scope, ttl);
        log(AXON_LOG_DEBUG, "memory->store: " + key + " [" + to_string(args.size()>=3?args[2]:Value::from(std::string("runtime"))) + "]");
        return Value::from(true);
    }

    // ── memory->get(key, scope="runtime") ────────────────────
    if (mod == "memory" && fn == "get") {
        if (args.empty()) return Value::null();
        std::string key   = to_string(args[0]);
        MemScope    scope = MemScope::Runtime;
        if (args.size() >= 2) {
            std::string sc = to_string(args[1]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
        }
        std::string val = _shared->memory.get(key, scope);
        return val.empty() ? Value::null() : Value::from(val);
    }

    // ── memory->has(key, scope="runtime") ────────────────────
    if (mod == "memory" && fn == "has") {
        if (args.empty()) return Value::from(false);
        std::string key   = to_string(args[0]);
        MemScope    scope = MemScope::Runtime;
        if (args.size() >= 2) {
            std::string sc = to_string(args[1]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
        }
        return Value::from(_shared->memory.has(key, scope));
    }

    // ── memory->remove(key, scope="runtime") ─────────────────
    if (mod == "memory" && fn == "remove") {
        if (args.empty()) return Value::from(false);
        MemScope scope = MemScope::Runtime;
        if (args.size() >= 2) {
            std::string sc = to_string(args[1]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
        }
        _shared->memory.remove(to_string(args[0]), scope);
        return Value::from(true);
    }

    // ── memory->clear(scope="runtime") ──────────────────────
    if (mod == "memory" && fn == "clear") {
        MemScope scope = MemScope::Runtime;
        if (!args.empty()) {
            std::string sc = to_string(args[0]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
            else if (sc == "all") { _shared->memory.clear_all(); return Value::from(true); }
        }
        _shared->memory.clear(scope);
        return Value::from(true);
    }

    // ── memory->keys(scope="runtime") ────────────────────────
    if (mod == "memory" && fn == "keys") {
        MemScope scope = MemScope::Runtime;
        if (!args.empty()) {
            std::string sc = to_string(args[0]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
        }
        Value r; r.kind = Value::Kind::List;
        for (auto& k : _shared->memory.keys(scope))
            r.list.push_back(Value::from(k));
        return r;
    }

    // ── memory->persist(path) ────────────────────────────────
    if (mod == "memory" && fn == "persist") {
        if (args.empty()) return Value::from(false);
        std::string path = to_string(args[0]);
        bool ok = _shared->memory.persist(path);
        log(AXON_LOG_INFO, "memory->persist: " + path
            + (ok ? " OK" : " FAILED"));
        return Value::from(ok);
    }

    // ── memory->load(path) ───────────────────────────────────
    if (mod == "memory" && fn == "load") {
        if (args.empty()) return Value::from(false);
        std::string path = to_string(args[0]);
        bool ok = _shared->memory.load(path);
        log(AXON_LOG_INFO, "memory->load: " + path
            + (ok ? " OK (" + std::to_string(
                _shared->memory.size(MemScope::Persistent)) + " entries)"
                  : " FAILED"));
        return Value::from(ok);
    }

    // ── memory->size(scope="runtime") ────────────────────────
    if (mod == "memory" && fn == "size") {
        MemScope scope = MemScope::Runtime;
        if (!args.empty()) {
            std::string sc = to_string(args[0]);
            if      (sc == "session")    scope = MemScope::Session;
            else if (sc == "shared")     scope = MemScope::Shared;
            else if (sc == "persistent") scope = MemScope::Persistent;
        }
        return Value::from((double)_shared->memory.size(scope));
    }

    // ── memory->embed(text) ─────────────────────────────────
    if (mod == "memory" && fn == "embed") {
        if (args.empty()) return Value::null();
        if (!_shared->semantic) _shared->semantic = std::make_unique<SemanticMemory>();
        auto vec = _shared->semantic->embed(to_string(args[0]));
        if (vec.empty()) { log(AXON_LOG_WARN,"memory->embed: no vector (Ollama running?)"); return Value::null(); }
        Value r; r.kind = Value::Kind::List;
        for (float f : vec) r.list.push_back(Value::from((double)f));
        return r;
    }

    // ── memory->index(text, metadata) ────────────────────────
    if (mod == "memory" && fn == "index") {
        if (args.empty()) return Value::from(false);
        if (!_shared->semantic) _shared->semantic = std::make_unique<SemanticMemory>();
        std::string text = to_string(args[0]);
        std::string meta = args.size() >= 2 ? to_string(args[1]) : "";
        bool ok = _shared->semantic->index(text, meta);
        if (ok) log(AXON_LOG_INFO,"memory->index: added ("+std::to_string(_shared->semantic->size())+" docs)");
        else    log(AXON_LOG_WARN,"memory->index: failed — is Ollama running?");
        return Value::from(ok);
    }

    // ── memory->search(query, top_k=5) ───────────────────────
    if (mod == "memory" && fn == "search") {
        if (args.empty()) { Value r; r.kind=Value::Kind::List; return r; }
        if (!_shared->semantic)    { Value r; r.kind=Value::Kind::List; return r; }
        std::string query = to_string(args[0]);
        int top_k = args.size() >= 2 ? (int)args[1].n : 5;
        auto results = _shared->semantic->search(query, top_k);
        Value out; out.kind = Value::Kind::List;
        for (auto& r : results) {
            Value entry; entry.kind = Value::Kind::Dict;
            entry.dict["text"]     = Value::from(r.text);
            entry.dict["metadata"] = Value::from(r.metadata);
            entry.dict["score"]    = Value::from((double)r.score);
            out.list.push_back(entry);
        }
        log(AXON_LOG_INFO,"memory->search: "+std::to_string(results.size())+" results");
        return out;
    }

    // ── memory->sem_size() ────────────────────────────────────
    if (mod == "memory" && fn == "sem_size") {
        return Value::from(_shared->semantic ? (double)_shared->semantic->size() : 0.0);
    }

    // ── memory->sem_persist(path) ─────────────────────────────
    if (mod == "memory" && fn == "sem_persist") {
        if (args.empty() || !_shared->semantic) return Value::from(false);
        bool ok = _shared->semantic->persist(to_string(args[0]));
        log(AXON_LOG_INFO,std::string("memory->sem_persist: ")+(ok?"OK":"FAILED"));
        return Value::from(ok);
    }

    // ── memory->sem_load(path) ────────────────────────────────
    if (mod == "memory" && fn == "sem_load") {
        if (args.empty()) return Value::from(false);
        if (!_shared->semantic) _shared->semantic = std::make_unique<SemanticMemory>();
        bool ok = _shared->semantic->load(to_string(args[0]));
        if (ok) {
            log(AXON_LOG_INFO, "memory->sem_load: OK ("
                + std::to_string(_shared->semantic->size()) + " docs)");
        } else {
            log(AXON_LOG_WARN,
                "memory->sem_load: FAILED — delete old .idx and re-index");
        }
        return Value::from(ok);
    }

    // ── whisper->transcribe(path) — stub (Phase 4) ───────────
    if (mod == "whisper" && fn == "transcribe") {
        log(AXON_LOG_INFO, "whisper->transcribe() — Phase 4 (Whisper.cpp)");
        return Value::from(std::string(""));
    }

    // ── sleep(seconds) — pause execution ────────────────────────
    if (mod == "sleep" || (mod == "" && fn == "sleep")) {
        double secs = args.empty() ? 0.0 : args[0].n;
        if (secs > 0) {
            log(AXON_LOG_INFO, "sleep: " + std::to_string(secs) + "s");
            sleep_checked(secs);
        }
        return Value::null();
    }

    // ── pdf->parse(path, mode) → Dict
    // mode="t" (default) → text extraction
    // mode="i"           → image extraction
    if (mod == "pdf" && fn == "parse") {
        if (args.empty()) return Value::null();
        std::string path         = to_string(args[0]);
        std::string what_extract = (args.size() > 1) ? to_string(args[1]) : "t";
        log(AXON_LOG_INFO, "pdf->parse: " + path + " [" + what_extract + "]");
        return parse_pdf_file(path, what_extract);
    }
    // ── pdf->text(path) → string (alias for file->read on pdf)
    if (mod == "pdf" && fn == "text") {
        if (args.empty()) return Value::null();
        std::ifstream f(to_string(args[0]), std::ios::binary);
        if (!f) return Value::null();
        std::ostringstream ss; ss << f.rdbuf();
        return read_pdf_file(ss.str());
    }

    // ── date->now() → "YYYY-MM-DD" ──────────────────────────
    if (mod == "date" && fn == "now") {
        time_t t = time(nullptr);
        struct tm* tm = localtime(&t);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
        return Value::from(std::string(buf));
    }
    // ── date->timestamp() → "YYYY-MM-DDTHH:MM:SS" ────────────
    if (mod == "date" && fn == "timestamp") {
        time_t t = time(nullptr);
        struct tm* tm = localtime(&t);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", tm);
        return Value::from(std::string(buf));
    }
    // ── date->unix() → number (unix timestamp) ───────────────
    if (mod == "date" && fn == "unix") {
        return Value::from((double)time(nullptr));
    }
    // ── date->format(date_str, fmt) ───────────────────────────
    if (mod == "date" && fn == "format") {
        if (args.size() < 2) return Value::null();
        std::string date_str = to_string(args[0]);
        std::string fmt      = to_string(args[1]);
        // Parse YYYY-MM-DD
        int y=0, m=0, d=0, H=0, M=0, S=0;
        sscanf(date_str.c_str(), "%d-%d-%dT%d:%d:%d", &y,&m,&d,&H,&M,&S);
        if (!d) sscanf(date_str.c_str(), "%d-%d-%d", &y,&m,&d);
        // Replace tokens
        auto rp = [](std::string s, const std::string& from,
                     const std::string& to) {
            size_t p = 0;
            while ((p = s.find(from, p)) != std::string::npos)
                { s.replace(p, from.size(), to); p += to.size(); }
            return s;
        };
        auto pad = [](int v, int w=2) {
            std::string s = std::to_string(v);
            while ((int)s.size() < w) s = "0" + s;
            return s;
        };
        fmt = rp(fmt, "YYYY", pad(y,4));
        fmt = rp(fmt, "YY",   pad(y%100));
        fmt = rp(fmt, "MM",   pad(m));
        fmt = rp(fmt, "DD",   pad(d));
        fmt = rp(fmt, "HH",   pad(H));
        fmt = rp(fmt, "mm",   pad(M));
        fmt = rp(fmt, "SS",   pad(S));
        return Value::from(fmt);
    }
    // ── date->parse(date_str) → Dict ─────────────────────────
    if (mod == "date" && fn == "parse") {
        if (args.empty()) return Value::null();
        std::string s = to_string(args[0]);
        int y=0, mo=0, d=0, H=0, M=0, S=0;
        sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y,&mo,&d,&H,&M,&S);
        if (!d) sscanf(s.c_str(), "%d-%d-%d", &y,&mo,&d);
        Value r; r.kind = Value::Kind::Dict;
        r.dict["year"]   = Value::from((double)y);
        r.dict["month"]  = Value::from((double)mo);
        r.dict["day"]    = Value::from((double)d);
        r.dict["hour"]   = Value::from((double)H);
        r.dict["minute"] = Value::from((double)M);
        r.dict["second"] = Value::from((double)S);
        return r;
    }
    // ── date->add(date_str, n, unit) → string ────────────────
    if (mod == "date" && (fn == "add" || fn == "sub")) {
        if (args.size() < 3) return Value::null();
        std::string s   = to_string(args[0]);
        int n           = (int)args[1].n * (fn == "sub" ? -1 : 1);
        std::string unit= to_string(args[2]);
        int y=0, mo=0, d=0;
        sscanf(s.c_str(), "%d-%d-%d", &y, &mo, &d);
        struct tm t{};
        t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
        if (unit.find("year")  != std::string::npos) t.tm_year += n;
        if (unit.find("month") != std::string::npos) t.tm_mon  += n;
        if (unit.find("day")   != std::string::npos) t.tm_mday += n;
        if (unit.find("hour")  != std::string::npos) t.tm_hour += n;
        mktime(&t); // normalize
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
        return Value::from(std::string(buf));
    }
    // ── date->diff(a, b, unit) → number ──────────────────────
    if (mod == "date" && fn == "diff") {
        if (args.size() < 2) return Value::from(0.0);
        std::string sa = to_string(args[0]);
        std::string sb = to_string(args[1]);
        std::string unit = args.size() >= 3 ? to_string(args[2]) : "days";
        auto parse_t = [](const std::string& s) -> time_t {
            int y=0,m=0,d=0; sscanf(s.c_str(),"%d-%d-%d",&y,&m,&d);
            struct tm t{}; t.tm_year=y-1900; t.tm_mon=m-1; t.tm_mday=d;
            return mktime(&t);
        };
        double diff_secs = difftime(parse_t(sb), parse_t(sa));
        if (unit.find("year")  != std::string::npos) return Value::from(diff_secs/31536000.0);
        if (unit.find("month") != std::string::npos) return Value::from(diff_secs/2592000.0);
        if (unit.find("hour")  != std::string::npos) return Value::from(diff_secs/3600.0);
        return Value::from(diff_secs/86400.0); // days default
    }
    // ── date->before/after(a, b) → bool ──────────────────────
    if (mod == "date" && (fn == "before" || fn == "after")) {
        if (args.size() < 2) return Value::from(false);
        std::string a = to_string(args[0]);
        std::string b = to_string(args[1]);
        bool res = fn == "before" ? (a < b) : (a > b);
        return Value::from(res);
    }

    // ── ocr-> ────────────────────────────────────────────────────
    if (mod == "ocr") {
        // Tesseract lookup: PATH first, then common install locations
        auto tess_cmd = []() -> std::string {
#if defined(_WIN32)
            // Check PATH
            if (std::system("tesseract --version >NUL 2>&1") == 0)
                return "tesseract";
            // Common Windows install paths
            const char* win_paths[] = {
                "C:\\Program Files\\Tesseract-OCR\\tesseract.exe",
                "C:\\Program Files (x86)\\Tesseract-OCR\\tesseract.exe",
                "C:\\Tesseract-OCR\\tesseract.exe",
                nullptr
            };
            for (int i = 0; win_paths[i]; ++i) {
                std::ifstream f(win_paths[i]);
                if (f.good()) return std::string("\"" + std::string(win_paths[i]) + "\"");
            }
            return "";
#else
            // Check PATH
            if (std::system("tesseract --version >/dev/null 2>&1") == 0)
                return "tesseract";
            // Common Unix paths
            const char* unix_paths[] = {
                "/usr/bin/tesseract",
                "/usr/local/bin/tesseract",
                "/opt/homebrew/bin/tesseract",
                nullptr
            };
            for (int i = 0; unix_paths[i]; ++i) {
                std::ifstream f(unix_paths[i]);
                if (f.good()) return std::string(unix_paths[i]);
            }
            return "";
#endif
        };
        auto tess_ok = [&tess_cmd]() -> bool { return !tess_cmd().empty(); };

        if (fn == "installed") {
            return Value::from(tess_ok());
        }

        if (fn == "read") {
            if (args.empty()) return Value::null();
            std::string img_path = to_string(args[0]);
            std::string lang = (args.size() > 1) ? to_string(args[1]) : "eng";

            if (!tess_ok()) {
                std::string msg = std::string("[ocr] Tesseract not found.\n") +
                    "  Windows: winget install UB-Mannheim.TesseractOCR\n" +
                    "  Linux:   sudo apt install tesseract-ocr tesseract-ocr-" + lang + "\n" +
                    "  macOS:   brew install tesseract";
                log(AXON_LOG_WARN, msg);
                return Value::from(msg);
            }

            TemporaryDirectory temporary;
            const std::string out_base = (temporary.path / "ocr").string();
            const std::string out_file = out_base + ".txt";
            auto cleanup = [&](const std::string& path) {
                std::error_code cleanup_error;
                std::filesystem::remove(path, cleanup_error);
                if (cleanup_error)
                    log(AXON_LOG_WARN, "[ocr] cannot remove " + path + ": " + cleanup_error.message());
            };

        #if defined(_WIN32)
            const std::string bat_file = out_base + ".bat";
            // Write .bat — avoids cmd.exe quoting issues with spaces in path.
            std::ofstream bat(bat_file);
            bat << "@echo off\n";
            bat << tess_cmd() << " \""
                << img_path << "\" \""
                << out_base << "\" -l " << lang << "\n";
            bat.close();
            if (!bat) {
                cleanup(bat_file);
                return Value::from(std::string("[ocr] cannot write command file"));
            }
            const int ocr_status = std::system(("\"" + bat_file + "\" >NUL 2>&1").c_str());
            cleanup(bat_file);
        #else
            const std::string cmd = tess_cmd()
                + " \"" + img_path + "\""
                + " \"" + out_base + "\""
                + " -l " + lang + " >/dev/null 2>&1";
            const int ocr_status = std::system(cmd.c_str());
        #endif
            if (ocr_status != 0) {
                cleanup(out_file);
                return Value::from(std::string("[ocr] Tesseract execution failed"));
            }

            std::ifstream rf(out_file);
            if (!rf) return Value::from(std::string("[ocr] no output file"));
            std::ostringstream oss; oss << rf.rdbuf(); rf.close();
            cleanup(out_file);

            if (oss.str().empty()) return Value::from(std::string("[ocr] no text detected"));
            log(AXON_LOG_INFO, "ocr->read: " + img_path + " [" + lang + "]");
            return Value::from(oss.str());
        }

        if (fn == "languages") {
            Value lst; lst.kind = Value::Kind::List;
            std::string langs_cmd = tess_cmd() + " --list-langs 2>&1";
#if defined(_WIN32)
            FILE* fp = _popen(langs_cmd.c_str(), "r");
#else
            FILE* fp = popen(langs_cmd.c_str(), "r");
#endif
            if (fp) {
                char buf[256]; bool first_line = true;
                while (fgets(buf, sizeof(buf), fp)) {
                    if (first_line) { first_line = false; continue; }
                    std::string ln(buf);
                    while (!ln.empty() && (ln.back()=='\n'||ln.back()=='\r'||ln.back()==' '))
                        ln.pop_back();
                    if (!ln.empty()) lst.list.push_back(Value::from(ln));
                }
#if defined(_WIN32)
                _pclose(fp);
#else
                pclose(fp);
#endif
            }
            return lst;
        }
    }

    // ── image->, codec->, hash-> ─────────────────────────────
    if (mod == "image" || mod == "codec" || mod == "hash") {
        auto ts = [this](const Value& v) { return to_string(v); };
        Value r = eval_modules(mod, fn, args, ts);
        if (!r.is_null() || mod != "hash") return r;
    }

    log(AXON_LOG_WARN, "unknown builtin: " + mod + "->" + fn);
    return Value::null();
}

} // namespace axon
