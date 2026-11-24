#include <map>
#include <set>
/**
 * runtime/include/interpreter.h
 * ───────────────────────────────
 * AST tree-walking interpreter for the Axon language.
 *
 * Execution model:
 *   1. Walk the Program node, register agents/flows/roles.
 *   2. Find the graph node and build a GraphEngine.
 *   3. Execute agents in topological order via the Scheduler.
 *   4. Each agent runs its main block (or task_graph).
 *   5. LLM calls go through the registered IProviderAdapter.
 */

#pragma once
#include "ast.h"
#include "sql_client.h"
#include "memory_store.h"
#include "semantic_memory.h"
#include "interfaces.hpp"
#include "graph_engine.cpp"
#include "actor.cpp"
#include "scheduler.cpp"
#include "runtime_memory.cpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <stdexcept>
#include <iostream>

namespace axon {

// ─────────────────────────────────────────────────────────────
// Runtime value (wraps AxonValue with easier construction)
// ─────────────────────────────────────────────────────────────
using Value = AxonValue;

// ─────────────────────────────────────────────────────────────
// Environment — lexical scope (chain of maps)
// ─────────────────────────────────────────────────────────────
class Env {
    friend class Interpreter;
public:
    explicit Env(std::shared_ptr<Env> parent = nullptr)
        : _interop(parent ? parent->_interop : std::make_shared<Value>(Value::null())), _parent(std::move(parent)) {}

    void set(const std::string& k, Value v) {
        _vars[k] = std::move(v);
    }

    bool contains(const std::string& k) const {
        if (_vars.count(k)) return true;
        if (_parent) return _parent->contains(k);
        return false;
    }
    Value get(const std::string& k) const {
        auto it = _vars.find(k);
        if (it != _vars.end()) return it->second;
        if (_parent) return _parent->get(k);
        return Value::null();
    }

    bool has(const std::string& k) const {
        if (_vars.count(k)) return true;
        return _parent ? _parent->has(k) : false;
    }

    Value interop_result() const { return *_interop; }
    void publish_interop(Value value) { *_interop = std::move(value); }
    void reset_interop() { _interop = std::make_shared<Value>(Value::null()); }

    // Assign to existing binding (walk up the chain)
    void assign(const std::string& k, Value v) {
        if (_vars.count(k)) { _vars[k] = std::move(v); return; }
        if (_parent)         { _parent->assign(k, std::move(v)); return; }
        _vars[k] = std::move(v); // create in current scope
    }



    // Snapshot for interop context injection
    std::unordered_map<std::string, Value> snapshot() const {
        std::unordered_map<std::string, Value> result;
        if (_parent) {
            auto p = _parent->snapshot();
            result.insert(p.begin(), p.end());
        }
        for (auto& [k, v] : _vars) result[k] = v;
        return result;
    }

private:
    std::shared_ptr<Value> _interop;
    std::unordered_map<std::string, Value> _vars;
    std::shared_ptr<Env>                   _parent;


};

// Shared-from-this requires inheriting; use wrapper instead
using EnvPtr = std::shared_ptr<Env>;
inline EnvPtr make_env(EnvPtr parent = nullptr) {
    return std::make_shared<Env>(std::move(parent));
}
inline EnvPtr child_env(EnvPtr parent) {
    return std::make_shared<Env>(std::move(parent));
}

// ─────────────────────────────────────────────────────────────
// Signals for control flow
// ─────────────────────────────────────────────────────────────
struct ReturnSignal { Value value; };
struct RaiseSignal  { std::string message; };

// ─────────────────────────────────────────────────────────────
// AgentDefinition — compiled agent from AST
// ─────────────────────────────────────────────────────────────
struct AgentDef {
    const char*       name;
    const char*       provider;
    const char*       model;
    double            temperature  = 0.7;
    int               max_tokens   = 4096;
    int               num_ctx      = 0;      // 0 = model default
    int               num_gpu      = -1;     // -1 = all layers on GPU, 0 = CPU only
    int               num_thread   = 0;      // 0 = auto
    int               main_gpu     = 0;      // which GPU (multi-GPU setups)
    int               num_batch    = 0;      // 0 = model default
    bool              f16_kv       = true;
    bool              low_vram     = false;
    bool              gpu_fallback = false;
    std::string       compute      = "";
    double            delay        = 0.0;   // seconds after agent finishes
    double            timeout      = 0.0;   // seconds max (0 = no limit)
    AxonNode*         main_block   = nullptr;
    AxonNode*         task_graph   = nullptr;
    AxonValidation    validation;
    std::unordered_map<std::string, AxonNode*> tasks;
    std::vector<std::string>                   task_order;
};

// ─────────────────────────────────────────────────────────────
// Interpreter
// ─────────────────────────────────────────────────────────────
class Interpreter {
public:
    explicit Interpreter();
    ~Interpreter();
    void set_source(const std::string& path, const std::string& src);
    static std::string val_to_str(const Value& v); // used by deep_set helper

    // Register a provider adapter (must be done before run)
    void register_provider(std::shared_ptr<IProviderAdapter> adapter);

    // Execute a parsed program
    Value run(AxonNode* program);

    // Only cancellation is safe to request from another thread. All other use is single-threaded.
    void request_cancel() noexcept { _cancelled->store(true, std::memory_order_relaxed); }
    // Call while idle before reusing an interpreter after cancellation.
    void reset_cancel() noexcept { _cancelled->store(false, std::memory_order_relaxed); }

    // Telemetry toggle
    bool telemetry = false;

private:
    struct TaskFrame;
    struct SharedState {
        MemoryStore memory;
        SqlRuntime sql;
        std::unique_ptr<SemanticMemory> semantic;
        std::recursive_timed_mutex native_mutex;
    };
    std::shared_ptr<SharedState> _shared = std::make_shared<SharedState>();
    std::unique_lock<std::recursive_timed_mutex> native_lock();
    Value exec_parallel(const std::vector<AxonNode*>& branches, EnvPtr env,
                        bool pipeline_group = false, Value input = Value::null());
    std::unique_ptr<Interpreter> fork_branch(EnvPtr env, EnvPtr& branch_env);
    void import_branch_closures(const Interpreter& branch, const Value& value);
    std::vector<std::unique_ptr<TaskFrame>> _owned_task_frames;
    std::map<std::string, Value> _parallel_task_updates;
    std::ostream* _stdout = &std::cout;
    std::ostream* _stderr = &std::cerr;
    Value with_timeout(double seconds, const std::string& owner,
                       const std::function<Value()>& action);
    void sleep_checked(double seconds) { _execution->sleep(seconds); }
    std::shared_ptr<std::atomic<bool>> _cancelled = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<ExecutionControl> _execution = std::make_shared<ExecutionControl>(_cancelled);
    // ── First pass: collect declarations ─────────────────────
    void collect(AxonNode* program, EnvPtr env);
    void collect_agent(AxonNode* node, EnvPtr env);

    // ── Execution ────────────────────────────────────────────
    Value exec_flow(EnvPtr env);
    Value exec_agent(const AgentDef& def, Value input, EnvPtr flow_env);
    Value exec_main(AxonNode* main, EnvPtr env);
    Value exec_task_graph(AxonNode* tg, const AgentDef& def, EnvPtr env);
    Value exec_task(const AgentDef& def, const std::string& name,
                    AxonNode* task, EnvPtr env, Value default_input,
                    bool explicit_input = false);
    Value resolve_task_input(const AgentDef& def, AxonNode* task,
                             EnvPtr env, Value default_input);
    const Value* find_task_output(const std::string& agent,
                                  const std::string& task) const;
    Value require_task_output(const std::string& agent, const std::string& task) const;
    void store_task_output(const AgentDef& def, const std::string& task, const Value& value);

    // ── Statement execution ──────────────────────────────────
    Value exec_stmt(AxonNode* node, EnvPtr env);
    Value exec_if(AxonNode* node, EnvPtr env);
    Value exec_for(AxonNode* node, EnvPtr env);
    Value exec_while(AxonNode* node, EnvPtr env);
    Value exec_match(AxonNode* node, EnvPtr env);
    Value exec_try(AxonNode* node, EnvPtr env);
    Value exec_interop(AxonNode* node, EnvPtr env);
    Value exec_body(AxonNodeList* body, EnvPtr env);

    // ── Expression evaluation ────────────────────────────────
    Value eval(AxonNode* node, EnvPtr env);
    Value eval_binary(AxonNode* node, EnvPtr env);
    Value eval_unary(AxonNode* node, EnvPtr env);
    Value eval_attr(AxonNode* node, EnvPtr env);
    Value eval_method(AxonNode* node, EnvPtr env);
    Value eval_call(AxonNode* node, EnvPtr env);
    Value eval_index(AxonNode* node, EnvPtr env);

    // ── LLM call ─────────────────────────────────────────────
    Value call_llm(const AgentDef& def,
                   const std::vector<std::string>& prompts,
                   const Value& input,
                   EnvPtr env);

    // ── Helpers ──────────────────────────────────────────────
    bool   truthy(const Value& v) const { return v.is_truthy(); }
    Value  to_value(const std::string& s) { return Value::from(s); }
    std::string to_string(const Value& v);
    Value  attr_of(const Value& obj, const std::string& attr);
    void   log(const std::string& msg);
    void   log(AxonLogLevel level, const std::string& msg);
    bool   eval_condition(const std::string& expr_str,
                          const Value& last_output, EnvPtr env);
    Value  exec_graph_from(const std::string& agent_name,
                           Value input, EnvPtr env);
    Value  parse_json_value(const std::string& s);
    Value  eval_task_call(AxonNode* node, EnvPtr env);
    Value  exec_index_assign(AxonNode* node, EnvPtr env);
    Value  exec_attr_assign(AxonNode* node, EnvPtr env);
    Value  eval_pipeline(AxonNode* node, EnvPtr env);
    Value  eval_builtin_call(const std::string& mod,
                             const std::string& fn,
                             const std::vector<Value>& args,
                             EnvPtr env);
    std::string extract_json(const std::string& s);
    void   print_metrics();

    // ── State ────────────────────────────────────────────────
    std::unordered_map<std::string,
        std::shared_ptr<IProviderAdapter>>   _providers;
    std::unordered_map<std::string, std::shared_ptr<std::timed_mutex>> _provider_locks;
    std::unordered_map<std::string, AgentDef> _agents;
    std::unique_ptr<GraphEngine>              _graph;
    // Output config (from flow output { })
    AxonOutputConfig                          _out_cfg;

    // Storage registry (named storage blocks)
    std::unordered_map<std::string, std::unordered_map<std::string,std::string>>
                                              _storages;

    // Retry counts for graph edges (agent_name -> count)
    std::unordered_map<std::string, int>      _retry_counts;

    // Source file for error reporting
    std::string              _source_file;
    std::vector<std::string> _source_lines;

    [[noreturn]] void runtime_error(const std::string& msg, int line = -1);

    // Active invocation frames distinguish missing output from a valid null,
    // and prevent an invocation from reading a previous run's task result.
    struct TaskFrame {
        const AgentDef* agent;
        std::unordered_map<std::string, Value> outputs;
    };
    std::vector<TaskFrame*> _task_frames;

    // Task outputs accessible from main: "AgentName.TaskName" -> Value
    std::unordered_map<std::string, Value>    _task_outputs;

    std::unique_ptr<RuntimeMemory>            _memory;
    bool                                      _debug_mode   = true;
    double                                    _flow_delay   = 0.0;
    double                                    _flow_timeout = 0.0;
    std::map<std::string, AxonNode*>          _flows;                 // named flow definitions
    std::set<std::string>                     _imported_files;        // guard against circular imports
    std::vector<AxonArena*>                   _import_arenas;         // keep imported file arenas alive
    // Closure capture: func_id → captured EnvPtr
    std::unordered_map<size_t, EnvPtr>        _closure_envs;
    std::shared_ptr<std::atomic<size_t>> _closure_id_counter = std::make_shared<std::atomic<size_t>>(0);
    std::shared_ptr<SessionMemory>            _sessions;
    AxonNode*                                 _graph_node = nullptr;
    const AgentDef*                           _current_agent = nullptr;
    std::string                               _expected_output;  // from task output=
    AxonNode*                                 _global_main_node = nullptr;
    EnvPtr                                    _global_env;
};

} // namespace axon
