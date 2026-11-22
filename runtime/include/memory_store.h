/**
 * runtime/include/memory_store.h
 * ────────────────────────────────
 * MemoryStore — key-value memory for Axon agents.
 *
 * Two modes:
 *   volatile   → in-process map (malloc), dies with process
 *   persistent → serialized to binary file, survives restarts
 *
 * Scopes:
 *   runtime    → volatile, per-agent, fastest
 *   session    → volatile, per-flow, cleared at flow end
 *   shared     → volatile, visible to all agents in the flow
 *   persistent → written to disk, survives process restart
 *
 * Phase 3B adds:
 *   semantic   → HNSW vector index + Ollama embeddings
 */

#pragma once
#include "platform.h"
#include "../interfaces/interfaces.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <cstdint>

namespace axon {

// ─────────────────────────────────────────────────────────────
// MemoryScope
// ─────────────────────────────────────────────────────────────
enum class MemScope {
    Runtime    = 0,   // volatile, fastest, per-agent
    Session    = 1,   // volatile, per-flow conversation
    Shared     = 2,   // volatile, cross-agent within flow
    Persistent = 3,   // disk-backed, survives restart
};

// ─────────────────────────────────────────────────────────────
// MemoryEntry — one stored value with metadata
// ─────────────────────────────────────────────────────────────
struct MemoryEntry {
    std::string value;     // serialized as string
    MemScope    scope;
    uint64_t    timestamp; // unix timestamp of last write
    uint32_t    ttl;       // 0 = no expiry (seconds)
};

// ─────────────────────────────────────────────────────────────
// MemoryStore
// ─────────────────────────────────────────────────────────────
class MemoryStore {
public:
    MemoryStore() = default;

    // ── Volatile operations ───────────────────────────────────
    void        store(const std::string& key,
                      const std::string& value,
                      MemScope           scope = MemScope::Runtime,
                      uint32_t           ttl   = 0);

    std::string get(const std::string& key,
                    MemScope           scope = MemScope::Runtime) const;

    bool        has(const std::string& key,
                    MemScope           scope = MemScope::Runtime) const;

    void        remove(const std::string& key,
                       MemScope           scope = MemScope::Runtime);

    void        clear(MemScope scope = MemScope::Runtime);
    void        clear_all();

    // List all keys in a scope
    std::vector<std::string> keys(MemScope scope = MemScope::Runtime) const;

    // ── Persistent operations ─────────────────────────────────
    // Write all entries in MemScope::Persistent to file
    bool persist(const std::string& path) const;

    // Load persisted entries from file into MemScope::Persistent
    bool load(const std::string& path);

    // ── Statistics ────────────────────────────────────────────
    size_t size(MemScope scope) const;

private:
    // One map per scope for O(1) access
    using ScopeMap = std::unordered_map<std::string, MemoryEntry>;

    ScopeMap& scope_map(MemScope s);
    const ScopeMap& scope_map(MemScope s) const;
    uint64_t now_ts() const;

    ScopeMap _runtime;
    ScopeMap _session;
    ScopeMap _shared;
    ScopeMap _persistent;

    // ── Binary file format ────────────────────────────────────
    // [4] magic  "AXNM"
    // [4] version (1)
    // [4] entry_count
    // For each entry:
    //   [4] key_len  [key_len] key
    //   [4] val_len  [val_len] value
    //   [8] timestamp
    //   [4] ttl
    static constexpr uint32_t MAGIC   = 0x414E4D58; // "AXNM"
    static constexpr uint32_t VERSION = 1;

    static void write_u32(std::ostream& out, uint32_t v);
    static void write_u64(std::ostream& out, uint64_t v);
    static void write_str(std::ostream& out, const std::string& s);
    static uint32_t    read_u32(std::istream& in);
    static uint64_t    read_u64(std::istream& in);
    static std::string read_str(std::istream& in);
};

} // namespace axon
