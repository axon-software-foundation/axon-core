/**
 * runtime/src/memory_store.cpp
 */
#include "../include/memory_store.h"
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace axon {

// ─────────────────────────────────────────────────────────────
// Scope map access
// ─────────────────────────────────────────────────────────────
MemoryStore::ScopeMap& MemoryStore::scope_map(MemScope s) {
    switch (s) {
        case MemScope::Session:    return _session;
        case MemScope::Shared:     return _shared;
        case MemScope::Persistent: return _persistent;
        default:                   return _runtime;
    }
}

const MemoryStore::ScopeMap& MemoryStore::scope_map(MemScope s) const {
    switch (s) {
        case MemScope::Session:    return _session;
        case MemScope::Shared:     return _shared;
        case MemScope::Persistent: return _persistent;
        default:                   return _runtime;
    }
}

uint64_t MemoryStore::now_ts() const {
    return (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// ─────────────────────────────────────────────────────────────
// Volatile operations
// ─────────────────────────────────────────────────────────────
void MemoryStore::store(const std::string& key,
                         const std::string& value,
                         MemScope           scope,
                         uint32_t           ttl) {
    MemoryEntry entry;
    entry.value     = value;
    entry.scope     = scope;
    entry.timestamp = now_ts();
    entry.ttl       = ttl;
    scope_map(scope)[key] = std::move(entry);
}

std::string MemoryStore::get(const std::string& key,
                              MemScope           scope) const {
    const auto& m = scope_map(scope);
    auto it = m.find(key);
    if (it == m.end()) return "";

    // Check TTL
    if (it->second.ttl > 0) {
        uint64_t elapsed = now_ts() - it->second.timestamp;
        if (elapsed > it->second.ttl) return ""; // expired
    }
    return it->second.value;
}

bool MemoryStore::has(const std::string& key, MemScope scope) const {
    const auto& m = scope_map(scope);
    auto it = m.find(key);
    if (it == m.end()) return false;
    if (it->second.ttl > 0) {
        uint64_t elapsed = now_ts() - it->second.timestamp;
        if (elapsed > it->second.ttl) return false;
    }
    return true;
}

void MemoryStore::remove(const std::string& key, MemScope scope) {
    scope_map(scope).erase(key);
}

void MemoryStore::clear(MemScope scope) {
    scope_map(scope).clear();
}

void MemoryStore::clear_all() {
    _runtime.clear();
    _session.clear();
    _shared.clear();
    _persistent.clear();
}

std::vector<std::string> MemoryStore::keys(MemScope scope) const {
    std::vector<std::string> result;
    for (auto& [k, _] : scope_map(scope))
        result.push_back(k);
    return result;
}

size_t MemoryStore::size(MemScope scope) const {
    return scope_map(scope).size();
}

// ─────────────────────────────────────────────────────────────
// Binary serialization helpers
// ─────────────────────────────────────────────────────────────
void MemoryStore::write_u32(std::ostream& out, uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), 4);
}

void MemoryStore::write_u64(std::ostream& out, uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), 8);
}

void MemoryStore::write_str(std::ostream& out, const std::string& s) {
    write_u32(out, (uint32_t)s.size());
    out.write(s.data(), (std::streamsize)s.size());
}

uint32_t MemoryStore::read_u32(std::istream& in) {
    uint32_t v = 0;
    in.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

uint64_t MemoryStore::read_u64(std::istream& in) {
    uint64_t v = 0;
    in.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

std::string MemoryStore::read_str(std::istream& in) {
    uint32_t len = read_u32(in);
    if (len > 64 * 1024 * 1024) return ""; // sanity: max 64MB per value
    std::string s(len, '\0');
    in.read(&s[0], (std::streamsize)len);
    return s;
}

// ─────────────────────────────────────────────────────────────
// Persist to file
// ─────────────────────────────────────────────────────────────
bool MemoryStore::persist(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    const auto& m = _persistent;

    write_u32(out, MAGIC);
    write_u32(out, VERSION);
    write_u32(out, (uint32_t)m.size());

    for (auto& [key, entry] : m) {
        write_str(out, key);
        write_str(out, entry.value);
        write_u64(out, entry.timestamp);
        write_u32(out, entry.ttl);
    }

    return out.good();
}

// ─────────────────────────────────────────────────────────────
// Load from file
// ─────────────────────────────────────────────────────────────
bool MemoryStore::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t magic   = read_u32(in);
    uint32_t version = read_u32(in);

    if (magic != MAGIC) return false;
    if (version > VERSION) return false; // forward compat

    uint32_t count = read_u32(in);
    if (count > 1'000'000) return false; // sanity limit

    uint64_t ts_now = now_ts();

    for (uint32_t i = 0; i < count; ++i) {
        std::string key   = read_str(in);
        std::string value = read_str(in);
        uint64_t    ts    = read_u64(in);
        uint32_t    ttl   = read_u32(in);

        if (!in) break;

        // Skip expired entries
        if (ttl > 0 && (ts_now - ts) > ttl) continue;

        MemoryEntry entry;
        entry.value     = std::move(value);
        entry.scope     = MemScope::Persistent;
        entry.timestamp = ts;
        entry.ttl       = ttl;
        _persistent[key] = std::move(entry);
    }

    return true;
}

} // namespace axon
