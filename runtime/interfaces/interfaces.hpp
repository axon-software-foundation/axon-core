/**
 * runtime/interfaces/interfaces.hpp
 * ───────────────────────────────────
 * Abstract C++ interfaces for every runtime module.
 *
 * CRITICAL DESIGN RULE:
 *   All modules communicate ONLY through these interfaces.
 *   Never include a concrete implementation from another module.
 *   This is what makes every phase independently replaceable.
 */

#pragma once
#include "../include/platform.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace axon {

/* ─────────────────────────────────────────────────────────────
 * Forward declarations
 * ───────────────────────────────────────────────────────────── */
struct AgentID   { std::string value; };
struct SessionID { std::string value; };
struct FlowID    { std::string value; };
struct TaskID    { std::string value; };

/* ─────────────────────────────────────────────────────────────
 * Agent state machine states
 * ───────────────────────────────────────────────────────────── */
enum class AgentState : uint8_t {
    IDLE       = 0,
    PENDING    = 1,
    READY      = 2,
    RUNNING    = 3,
    CHECKPOINT = 4,
    EMITTING   = 5,
    SUCCESS    = 6,
    FAILED     = 7,
    FLAGGED    = 8,   // hallucination validator rejected output
};

std::string agent_state_name(AgentState s);

/* ─────────────────────────────────────────────────────────────
 * Value — runtime value type
 * ───────────────────────────────────────────────────────────── */
struct AxonValue {
    enum class Kind { Null, Bool, Number, String, List, Dict, Bytes };
    Kind kind = Kind::Null;

    bool        b   = false;
    double      n   = 0.0;
    std::string s;
    std::vector<AxonValue>                        list;
    std::unordered_map<std::string, AxonValue>    dict;
    std::vector<uint8_t>                          bytes;  // Arrow / raw

    static AxonValue null()               { return {}; }
    static AxonValue from(bool v)         { AxonValue x; x.kind=Kind::Bool;   x.b=v; return x; }
    static AxonValue from(double v)       { AxonValue x; x.kind=Kind::Number; x.n=v; return x; }
    static AxonValue from(std::string v)  { AxonValue x; x.kind=Kind::String; x.s=std::move(v); return x; }
    bool is_null()   const { return kind == Kind::Null; }
    bool is_truthy() const {
        switch (kind) {
            case Kind::Null:   return false;
            case Kind::Bool:   return b;
            case Kind::Number: return n != 0.0;
            case Kind::String: return !s.empty();
            case Kind::List:   return !list.empty();
            case Kind::Dict:   return !dict.empty();
            case Kind::Bytes:  return !bytes.empty();
        }
        return false;
    }
};

/* ─────────────────────────────────────────────────────────────
 * IMemoryEngine — Phase 1: RuntimeMemory implemented
 *                 Phase 2: SemanticMemory, PersistentMemory, etc.
 * ───────────────────────────────────────────────────────────── */
class IMemoryEngine {
public:
    virtual ~IMemoryEngine() = default;

    virtual void       store(const std::string& key,
                             const AxonValue&   value)       = 0;
    virtual AxonValue  retrieve(const std::string& key)      = 0;
    virtual bool       exists(const std::string& key)        = 0;
    virtual void       forget(const std::string& key)        = 0;
    virtual void       clear()                               = 0;

    // Phase 2: semantic search — default returns empty (not implemented yet)
    virtual std::vector<std::pair<std::string, float>>
    search_similar(const std::string& /*query*/, size_t /*top_k*/) {
        return {};
    }

    virtual std::string type_name() const = 0;
};

/* ─────────────────────────────────────────────────────────────
 * ISessionEngine
 * ───────────────────────────────────────────────────────────── */
struct ConversationTurn {
    std::string role;      // "user" | "assistant" | "system"
    std::string content;
    int64_t     timestamp_ms;
};

class ISessionEngine {
public:
    virtual ~ISessionEngine() = default;

    virtual SessionID   create()                                     = 0;
    virtual void        append(const SessionID& id,
                                const ConversationTurn& turn)        = 0;
    virtual std::vector<ConversationTurn>
                        history(const SessionID& id,
                                size_t max_turns = 0)                = 0;
    virtual void        persist(const SessionID& id)                 = 0;
    virtual bool        rehydrate(const SessionID& id)               = 0;
    virtual void        close(const SessionID& id)                   = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IValidator — Cognitive / Hallucination validator
 * ───────────────────────────────────────────────────────────── */
struct ValidationConfig {
    double      confidence_threshold = 0.0;   // 0 = disabled
    std::string expected_format;              // "" = disabled
    int         max_retries         = 3;
    enum class OnFail { Retry, Flag, Skip, Raise } on_fail = OnFail::Retry;
};

struct ValidationResult {
    bool        passed;
    double      confidence_score;
    std::string reason;           // if !passed
};

class IValidator {
public:
    virtual ~IValidator() = default;

    virtual ValidationResult validate(
        const AxonValue&      output,
        const ValidationConfig& config) = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IActor — one per agent
 * ───────────────────────────────────────────────────────────── */
struct AgentMessage {
    AgentID     from;
    std::string type;         // "input" | "signal" | "result"
    AxonValue   payload;
};

class IActor {
public:
    virtual ~IActor() = default;

    virtual AgentID     id()    const = 0;
    virtual AgentState  state() const = 0;

    virtual void        send(const AgentMessage& msg)  = 0;
    virtual std::optional<AgentMessage> recv()         = 0;

    virtual AxonValue   run(const AxonValue& input)    = 0;
    virtual void        checkpoint()                   = 0;
    virtual void        reset()                        = 0;

    // State transition — validated against the FSM
    virtual bool        transition(AgentState next)    = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IScheduler
 * ───────────────────────────────────────────────────────────── */
struct ScheduledTask {
    AgentID   agent_id;
    AxonValue input;
    int       priority    = 0;   // higher = sooner
    int       max_retries = 3;
};

class IScheduler {
public:
    virtual ~IScheduler() = default;

    virtual void      submit(const ScheduledTask& task)           = 0;
    virtual AxonValue await_result(const AgentID& id)             = 0;
    virtual void      cancel(const AgentID& id)                   = 0;
    virtual size_t    queue_depth()                         const  = 0;

    virtual void      start(size_t thread_count = 0)              = 0;
    virtual void      stop()                                       = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IGraphEngine
 * ───────────────────────────────────────────────────────────── */
struct GraphEdge {
    AgentID     from;
    AgentID     to;
    bool        on_success  = true;
    bool        on_fail     = false;
    int         max_retries = 0;
    std::string condition;        // "" = unconditional
};

class IGraphEngine {
public:
    virtual ~IGraphEngine() = default;

    virtual void    add_node(const AgentID& id)                     = 0;
    virtual void    add_edge(const GraphEdge& edge)                 = 0;
    virtual void    remove_node(const AgentID& id)                  = 0;

    // Traversal
    virtual std::vector<AgentID> successors(const AgentID& id)      = 0;
    virtual std::vector<AgentID> predecessors(const AgentID& id)    = 0;
    virtual std::vector<AgentID> topological_order()                 = 0;
    virtual bool                 has_cycle()                         = 0;
    virtual std::vector<AgentID> bfs(const AgentID& start)          = 0;
    virtual std::vector<AgentID> dfs(const AgentID& start)          = 0;

    // Routing (Phase 3: Dijkstra / A*)
    virtual std::vector<AgentID> shortest_path(
        const AgentID& from, const AgentID& to) {
        return {};  // default: not yet implemented
    }

    virtual size_t node_count() const = 0;
    virtual size_t edge_count() const = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IProviderAdapter — abstracts every LLM provider
 * ───────────────────────────────────────────────────────────── */
struct LLMRequest {
    std::string              provider;
    std::string              model;
    std::vector<ConversationTurn> messages;
    double                   temperature = 0.7;
    int                      max_tokens  = 4096;
    int                      num_ctx     = 0;
    int                      num_gpu     = -1;   // -1=all GPU, 0=CPU only
    int                      num_thread  = 0;    // 0=auto
    int                      main_gpu    = 0;
    int                      num_batch   = 0;    // 0=default
    bool                     f16_kv      = true;
    bool                     low_vram    = false;
    bool                     gpu_fallback= false;
    std::string              compute     = "";   // auto/gpu/cpu
};

struct LLMResponse {
    std::string content;
    int         tokens_in;
    int         tokens_out;
    double      latency_ms;
    std::string finish_reason;
};

class IProviderAdapter {
public:
    virtual ~IProviderAdapter() = default;

    virtual std::string   provider_name()               const = 0;
    virtual LLMResponse   complete(const LLMRequest& req)     = 0;
    virtual bool          is_available()                      = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IMetricsEngine
 * ───────────────────────────────────────────────────────────── */
struct AgentMetrics {
    AgentID     agent_id;
    double      latency_ms        = 0;
    int         tokens_in         = 0;
    int         tokens_out        = 0;
    double      confidence_score  = 0;
    int         retry_count       = 0;
    int         memory_hits       = 0;
    bool        hallucination_flag = false;
};

class IMetricsEngine {
public:
    virtual ~IMetricsEngine() = default;

    virtual void record(const AgentMetrics& m)                    = 0;
    virtual AgentMetrics aggregate(const AgentID& id)             = 0;
    virtual void export_otel()                                    = 0;  // Phase 4
    virtual void reset(const AgentID& id)                         = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IInteropBridge
 * ───────────────────────────────────────────────────────────── */
struct InteropResult {
    std::string lang;
    std::string stdout_buf;
    std::string stderr_buf;
    AxonValue   result;
    std::string error;     // "" = success
};

class IInteropBridge {
public:
    virtual ~IInteropBridge() = default;

    virtual std::string  lang_name()                              const = 0;
    virtual InteropResult execute(const std::string& code,
                                  const std::unordered_map<std::string,
                                        AxonValue>& ctx)               = 0;
};

/* ─────────────────────────────────────────────────────────────
 * IPhysicalMesh — Phase 5: Ultrasonic + network
 * ───────────────────────────────────────────────────────────── */
struct MeshSignal {
    FlowID      flow_id;
    SessionID   session_id;
    uint8_t     state_byte;
    std::string channel_hint;   // "grpc://192.168.x.x:50051"
};

class IPhysicalMesh {
public:
    virtual ~IPhysicalMesh() = default;

    virtual void broadcast(const MeshSignal& sig) = 0;
    virtual std::optional<MeshSignal> listen(int timeout_ms) = 0;
    virtual bool available() const = 0;
};

} // namespace axon