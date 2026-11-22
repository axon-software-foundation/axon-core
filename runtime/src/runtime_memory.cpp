/**
 * runtime/src/runtime_memory.cpp
 * ────────────────────────────────
 * Phase 1 implementation of IMemoryEngine.
 * Volatile in-process memory — fastest possible, no persistence.
 *
 * Phase 2 will add: SemanticMemory, PersistentMemory, SharedMemory
 * All via the same IMemoryEngine interface — zero changes to callers.
 */

#pragma once
#include "../interfaces/interfaces.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <string>
#include <stdexcept>

namespace axon {

class RuntimeMemory final : public IMemoryEngine {
public:
    RuntimeMemory() = default;

    void store(const std::string& key, const AxonValue& value) override {
        std::unique_lock<std::shared_mutex> lk(_mu);
        _store[key] = value;
    }

    AxonValue retrieve(const std::string& key) override {
        std::shared_lock<std::shared_mutex> lk(_mu);
        auto it = _store.find(key);
        if (it == _store.end()) return AxonValue::null();
        return it->second;
    }

    bool exists(const std::string& key) override {
        std::shared_lock<std::shared_mutex> lk(_mu);
        return _store.count(key) > 0;
    }

    void forget(const std::string& key) override {
        std::unique_lock<std::shared_mutex> lk(_mu);
        _store.erase(key);
    }

    void clear() override {
        std::unique_lock<std::shared_mutex> lk(_mu);
        _store.clear();
    }

    std::string type_name() const override { return "RuntimeMemory"; }

    size_t size() const {
        std::shared_lock<std::shared_mutex> lk(_mu);
        return _store.size();
    }

private:
    mutable std::shared_mutex                     _mu;
    std::unordered_map<std::string, AxonValue>    _store;
};


/* ─────────────────────────────────────────────────────────────
 * Session Memory — ring-buffer per session (Phase 1)
 * ───────────────────────────────────────────────────────────── */
class SessionMemory final : public ISessionEngine {
public:
    explicit SessionMemory(size_t max_turns_per_session = 100)
        : _max_turns(max_turns_per_session)
    {}

    SessionID create() override {
        std::unique_lock<std::shared_mutex> lk(_mu);
        SessionID id{ generate_id() };
        _sessions[id.value] = {};
        return id;
    }

    void append(const SessionID& id,
                const ConversationTurn& turn) override {
        std::unique_lock<std::shared_mutex> lk(_mu);
        auto& history = _sessions[id.value];
        history.push_back(turn);
        // Ring-buffer behaviour: drop oldest when over limit
        if (history.size() > _max_turns)
            history.erase(history.begin());
    }

    std::vector<ConversationTurn>
    history(const SessionID& id, size_t max_turns = 0) override {
        std::shared_lock<std::shared_mutex> lk(_mu);
        auto it = _sessions.find(id.value);
        if (it == _sessions.end()) return {};

        const auto& h = it->second;
        if (max_turns == 0 || max_turns >= h.size()) return h;

        // Return last max_turns entries
        return { h.end() - static_cast<ptrdiff_t>(max_turns), h.end() };
    }

    // Phase 2: implement actual disk persistence
    void persist(const SessionID&)   override { /* Phase 2 */ }
    bool rehydrate(const SessionID&) override { return false; /* Phase 2 */ }

    void close(const SessionID& id) override {
        std::unique_lock<std::shared_mutex> lk(_mu);
        _sessions.erase(id.value);
    }

    size_t session_count() const {
        std::shared_lock<std::shared_mutex> lk(_mu);
        return _sessions.size();
    }

private:
    static std::string generate_id() {
        // Simple UUID v4 placeholder — Phase 2 uses proper UUID lib
        static std::atomic<uint64_t> counter{0};
        return "session-" + std::to_string(++counter);
    }

    size_t _max_turns;
    mutable std::shared_mutex _mu;
    std::unordered_map<std::string,
        std::vector<ConversationTurn>> _sessions;
};

} // namespace axon
