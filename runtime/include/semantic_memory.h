#pragma once
/**
 * runtime/include/semantic_memory.h
 * ─────────────────────────────────
 * SemanticMemory — HNSW vector index with Ollama embeddings.
 *
 * Phase 3B:  provider = ollama  (nomic-embed-text, mxbai-embed-large, etc.)
 * Phase 5:   provider = openai  (text-embedding-3-small, ada-002)
 *            provider = cohere  (embed-english-v3.0)
 *
 * Usage from Axon:
 *   memory->embed("some text")               -> vector (internal)
 *   memory->index("text", "optional_meta")  -> store in HNSW
 *   memory->search("query", top_k=5)        -> list of {text, score, meta}
 *   memory->persist("./idx.bin")            -> save index + vectors
 *   memory->load("./idx.bin")               -> restore
 */

#include "platform.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <stdexcept>
#include <cstring>

// ── hnswlib (header-only) ─────────────────────────────────────
#include <hnswlib.h>
#include <space_l2.h>
#include <hnswalg.h>

namespace axon {

// ─────────────────────────────────────────────────────────────
// EmbedConfig — provider settings
// ─────────────────────────────────────────────────────────────
struct EmbedConfig {
    std::string provider  = "ollama";
    std::string model     = "nomic-embed-text";
    std::string endpoint  = "http://localhost:11434";
    std::string api_key;          // for OpenAI / Cohere (Phase 5)
    int         dim       = 768;
};

// ─────────────────────────────────────────────────────────────
// SemanticEntry — one stored document
// ─────────────────────────────────────────────────────────────
struct SemanticEntry {
    std::string          text;
    std::string          metadata;
    std::vector<float>   embedding;
};

// ─────────────────────────────────────────────────────────────
// SemanticMemory
// ─────────────────────────────────────────────────────────────
class SemanticMemory {
public:
    explicit SemanticMemory(const EmbedConfig& cfg = EmbedConfig{});
    ~SemanticMemory() = default;

    // ── Embedding ─────────────────────────────────────────────
    // Get embedding vector for text. Returns empty on failure.
    std::vector<float> embed(const std::string& text);

    // ── Index operations ──────────────────────────────────────
    // Add text (with optional metadata) to the HNSW index.
    bool index(const std::string& text,
               const std::string& metadata = "");

    // ── Search ────────────────────────────────────────────────
    struct SearchResult {
        std::string text;
        std::string metadata;
        float       score;    // cosine similarity 0..1
    };

    std::vector<SearchResult> search(const std::string& query,
                                     int top_k = 5);

    // ── Persistence ───────────────────────────────────────────
    bool persist(const std::string& path) const;
    bool load(const std::string& path);

    // ── Stats ─────────────────────────────────────────────────
    size_t size() const { return _entries.size(); }
    bool   ready() const { return _hnsw != nullptr; }
    const EmbedConfig& config() const { return _cfg; }

private:
    EmbedConfig  _cfg;
    int          _dim = 768;

    // HNSW index
    std::unique_ptr<hnswlib::L2Space>                    _space;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>>     _hnsw;
    size_t _max_elements = 10000;
    size_t _current_label = 0;

    // Document store (label -> entry)
    std::unordered_map<size_t, SemanticEntry> _entries;

    void  ensure_index();

    // HTTP helper — minimal implementation
    std::string http_post(const std::string& url,
                          const std::string& json_body);

    // JSON helpers (no external dependency)
    std::string  json_escape(const std::string& s);
    std::vector<float> parse_float_array(const std::string& json);
    std::string  extract_json_field(const std::string& json,
                                    const std::string& field);

    // Binary format constants
    static constexpr uint32_t MAGIC   = 0x53454D58; // "SEMX"
    static constexpr uint32_t VERSION = 1;

    static void        write_u32(std::ostream& o, uint32_t v);
    static void        write_u64(std::ostream& o, uint64_t v);
    static void        write_str(std::ostream& o, const std::string& s);
    static void        write_floats(std::ostream& o,
                                    const std::vector<float>& v);
    static uint32_t    read_u32(std::istream& i);
    static uint64_t    read_u64(std::istream& i);
    static std::string read_str(std::istream& i);
    static std::vector<float> read_floats(std::istream& i, uint32_t n);
};

} // namespace axon
