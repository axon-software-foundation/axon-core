/**
 * runtime/src/semantic_memory.cpp
 */
#include "../include/semantic_memory.h"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>

// Platform HTTP
#if defined(_WIN32) || defined(_WIN64)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
  #pragma comment(lib,"ws2_32.lib")
  #endif
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
#endif

namespace axon {

// ─────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────
SemanticMemory::SemanticMemory(const EmbedConfig& cfg)
    : _cfg(cfg), _dim(cfg.dim)
{
}

void SemanticMemory::ensure_index() {
    if (_hnsw) return;
    _space = std::make_unique<hnswlib::L2Space>(_dim);
    _hnsw  = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                 _space.get(), _max_elements,
                 /*M=*/16, /*ef_construction=*/200);
}

// ─────────────────────────────────────────────────────────────
// Embed — call Ollama /api/embed
// ─────────────────────────────────────────────────────────────
std::vector<float> SemanticMemory::embed(const std::string& text) {
    std::string body = "{\"model\":\"" + json_escape(_cfg.model) +
                       "\",\"input\":\"" + json_escape(text) + "\"}";

    std::string url = _cfg.endpoint + "/api/embed";
    std::string resp = http_post(url, body);

    if (resp.empty()) return {};

    // Parse embeddings array from response
    // Ollama /api/embed returns: {"embeddings":[[...]]}
    auto emb_pos = resp.find("\"embeddings\"");
    if (emb_pos == std::string::npos) {
        // Try /api/embeddings legacy: {"embedding":[...]}
        emb_pos = resp.find("\"embedding\"");
        if (emb_pos == std::string::npos) return {};
        auto arr_start = resp.find("[", emb_pos);
        if (arr_start == std::string::npos) return {};
        return parse_float_array(resp.substr(arr_start));
    }

    // Find the inner array [[...]]
    auto outer = resp.find("[", emb_pos);
    if (outer == std::string::npos) return {};
    auto inner = resp.find("[", outer + 1);
    if (inner == std::string::npos) return {};
    return parse_float_array(resp.substr(inner));
}

// ─────────────────────────────────────────────────────────────
// Index — add a document
// ─────────────────────────────────────────────────────────────
bool SemanticMemory::index(const std::string& text,
                            const std::string& metadata) {
    std::vector<float> vec = embed(text);
    if (vec.empty()) return false;

    // Resize if model returns different dim
    if ((int)vec.size() != _dim) {
        _dim = (int)vec.size();
        _cfg.dim = _dim;
        // Reset index with new dim
        _space.reset();
        _hnsw.reset();
        _entries.clear();
        _current_label = 0;
    }

    ensure_index();

    // Grow index if needed
    if (_current_label >= _max_elements) {
        _max_elements *= 2;
        _hnsw->resizeIndex(_max_elements);
    }

    SemanticEntry entry;
    entry.text      = text;
    entry.metadata  = metadata;
    entry.embedding = vec;

    _hnsw->addPoint(vec.data(), _current_label);
    _entries[_current_label] = std::move(entry);
    ++_current_label;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Search — find K nearest neighbors
// ─────────────────────────────────────────────────────────────
std::vector<SemanticMemory::SearchResult>
SemanticMemory::search(const std::string& query, int top_k) {
    if (!_hnsw || _current_label == 0) return {};

    std::vector<float> qvec = embed(query);
    if (qvec.empty() || (int)qvec.size() != _dim) return {};

    int k = std::min(top_k, (int)_current_label);
    auto result = _hnsw->searchKnn(qvec.data(), (size_t)k);

    std::vector<SearchResult> out;
    while (!result.empty()) {
        auto [dist, label] = result.top();
        result.pop();
        auto it = _entries.find(label);
        if (it == _entries.end()) continue;

        SearchResult sr;
        sr.text     = it->second.text;
        sr.metadata = it->second.metadata;
        // Convert L2 distance to similarity score 0..1
        sr.score    = 1.0f / (1.0f + dist);
        out.push_back(std::move(sr));
    }
    std::reverse(out.begin(), out.end()); // best first
    return out;
}

// ─────────────────────────────────────────────────────────────
// Persistence
// ─────────────────────────────────────────────────────────────
// ── File format (single file) ────────────────────────────────
// [4]  MAGIC "SEMX"
// [4]  VERSION
// [4]  dim
// [8]  current_label
// [4]  entry_count
// [str] provider
// [str] model
// For each entry:
//   [8]  label
//   [str] text
//   [str] metadata
//   [4+n*4] embedding (count + floats)
// [4]  HNSW_MAGIC "HNSW" (marker before native HNSW block)
// [native HNSW binary follows — written via saveIndex to temp then appended]

bool SemanticMemory::persist(const std::string& path) const {
    if (!_hnsw || _entries.empty()) return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    // Header + document store
    write_u32(out, MAGIC);
    write_u32(out, VERSION);
    write_u32(out, (uint32_t)_dim);
    write_u64(out, (uint64_t)_current_label);
    write_u32(out, (uint32_t)_entries.size());
    write_str(out, _cfg.provider);
    write_str(out, _cfg.model);

    for (auto& [label, entry] : _entries) {
        write_u64(out, (uint64_t)label);
        write_str(out, entry.text);
        write_str(out, entry.metadata);
        write_floats(out, entry.embedding);
    }

    // Write native HNSW block via temp file then append
    static const uint32_t HNSW_MARKER = 0x484E5357; // "HNSW"
    write_u32(out, HNSW_MARKER);

    std::string tmp_path = path + ".hnsw_tmp";
    try {
        _hnsw->saveIndex(tmp_path);
        std::ifstream tmp(tmp_path, std::ios::binary);
        if (tmp) {
            out << tmp.rdbuf();
            tmp.close();
        }
        std::remove(tmp_path.c_str());
    } catch (...) {
        std::remove(tmp_path.c_str());
        return false;
    }

    return out.good();
}

bool SemanticMemory::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    // Read header
    uint32_t magic = read_u32(in);
    uint32_t ver   = read_u32(in);
    if (magic != MAGIC) {
        // Old format or corrupt file — cannot load
        return false;
    }
    if (ver > VERSION) return false;

    int      new_dim   = (int)read_u32(in);
    size_t   new_label = (size_t)read_u64(in);
    uint32_t count     = read_u32(in);
    std::string provider = read_str(in);
    std::string model    = read_str(in);

    // Read document store
    struct RawEntry {
        size_t label;
        std::string text, metadata;
        std::vector<float> embedding;
    };
    std::vector<RawEntry> raw;
    raw.reserve(count);

    for (uint32_t i = 0; i < count && in; ++i) {
        RawEntry e;
        e.label     = (size_t)read_u64(in);
        e.text      = read_str(in);
        e.metadata  = read_str(in);
        e.embedding = read_floats(in, (uint32_t)new_dim);
        raw.push_back(std::move(e));
    }

    // Check for HNSW marker
    static const uint32_t HNSW_MARKER = 0x484E5357;
    uint32_t marker = read_u32(in);
    bool has_hnsw_block = (marker == HNSW_MARKER);

    // Apply config
    _entries.clear();
    _space.reset();
    _hnsw.reset();
    _dim           = new_dim;
    _current_label = new_label;
    _cfg.provider  = provider;
    _cfg.model     = model;
    _cfg.dim       = _dim;
    _max_elements  = std::max((size_t)count + 1000, (size_t)10000);
    _space         = std::make_unique<hnswlib::L2Space>(_dim);

    if (has_hnsw_block) {
        // Extract HNSW block to temp file and load natively
        std::string tmp_path = path + ".hnsw_tmp";
        {
            std::ofstream tmp(tmp_path, std::ios::binary | std::ios::trunc);
            tmp << in.rdbuf();
        }
        try {
            _hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                        _space.get(), tmp_path, false, _max_elements);
            std::remove(tmp_path.c_str());
        } catch (...) {
            std::remove(tmp_path.c_str());
            has_hnsw_block = false; // fall through to rebuild
        }
    }

    if (!has_hnsw_block) {
        // Rebuild HNSW from stored embeddings
        _hnsw = std::make_unique<hnswlib::HierarchicalNSW<float>>(
                    _space.get(), _max_elements, 16, 200);
        std::sort(raw.begin(), raw.end(),
                  [](const RawEntry& a, const RawEntry& b){
                      return a.label < b.label; });
        for (auto& e : raw) {
            if (!e.embedding.empty()) {
                try { _hnsw->addPoint(e.embedding.data(), e.label); }
                catch (...) {}
            }
        }
    }

    // Restore document store
    for (auto& e : raw) {
        SemanticEntry se;
        se.text      = e.text;
        se.metadata  = e.metadata;
        se.embedding = e.embedding;
        _entries[e.label] = std::move(se);
    }

    return !_entries.empty();
}

// ─────────────────────────────────────────────────────────────
// HTTP POST — minimal TCP implementation
// ─────────────────────────────────────────────────────────────
std::string SemanticMemory::http_post(const std::string& url,
                                       const std::string& body) {
    // Parse URL: http://host:port/path
    std::string host, path;
    int port = 80;

    std::string u = url;
    if (u.substr(0, 7) == "http://") u = u.substr(7);
    auto slash = u.find('/');
    if (slash == std::string::npos) { host = u; path = "/"; }
    else { host = u.substr(0, slash); path = u.substr(slash); }

    auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        host = host.substr(0, colon);
    }

    // Build HTTP request
    std::string req =
        "POST " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;

#if defined(_WIN32) || defined(_WIN64)
    WSADATA wd; WSAStartup(MAKEWORD(2,2), &wd);
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return "";
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { closesocket(sock); return ""; }
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((u_short)port);
    memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    if (connect(sock, (SOCKADDR*)&sa, sizeof(sa)) != 0)
        { closesocket(sock); return ""; }
    send(sock, req.c_str(), (int)req.size(), 0);
    std::string resp;
    char buf[4096];
    int n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, n);
    closesocket(sock);
    WSACleanup();
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { close(sock); return ""; }
    struct sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    memcpy(&sa.sin_addr, he->h_addr, he->h_length);
    if (::connect(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0)
        { close(sock); return ""; }
    ::send(sock, req.c_str(), (int)req.size(), 0);
    std::string resp;
    char buf[4096]; int n;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0)
        resp.append(buf, n);
    ::close(sock);
#endif

    // Strip HTTP headers — return body only
    auto sep = resp.find("\r\n\r\n");
    return sep == std::string::npos ? resp : resp.substr(sep + 4);
}

// ─────────────────────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────────────────────
std::string SemanticMemory::json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

std::vector<float> SemanticMemory::parse_float_array(const std::string& json) {
    std::vector<float> result;
    auto start = json.find('[');
    auto end   = json.find(']', start);
    if (start == std::string::npos || end == std::string::npos) return result;

    std::string inner = json.substr(start + 1, end - start - 1);
    std::istringstream ss(inner);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // trim whitespace
        size_t a = token.find_first_not_of(" \t\n\r");
        if (a == std::string::npos) continue;
        try { result.push_back(std::stof(token.substr(a))); }
        catch (...) {}
    }
    return result;
}

// ─────────────────────────────────────────────────────────────
// Binary I/O helpers
// ─────────────────────────────────────────────────────────────
void SemanticMemory::write_u32(std::ostream& o, uint32_t v)
    { o.write(reinterpret_cast<const char*>(&v), 4); }

void SemanticMemory::write_u64(std::ostream& o, uint64_t v)
    { o.write(reinterpret_cast<const char*>(&v), 8); }

void SemanticMemory::write_str(std::ostream& o, const std::string& s) {
    write_u32(o, (uint32_t)s.size());
    o.write(s.data(), (std::streamsize)s.size());
}

void SemanticMemory::write_floats(std::ostream& o,
                                   const std::vector<float>& v) {
    // Do NOT write count — read_floats receives dim from the file header
    o.write(reinterpret_cast<const char*>(v.data()),
            (std::streamsize)(v.size() * sizeof(float)));
}

uint32_t SemanticMemory::read_u32(std::istream& i) {
    uint32_t v = 0;
    i.read(reinterpret_cast<char*>(&v), 4);
    return v;
}

uint64_t SemanticMemory::read_u64(std::istream& i) {
    uint64_t v = 0;
    i.read(reinterpret_cast<char*>(&v), 8);
    return v;
}

std::string SemanticMemory::read_str(std::istream& i) {
    uint32_t len = read_u32(i);
    if (len > 64 * 1024 * 1024) return "";
    std::string s(len, '\0');
    i.read(&s[0], (std::streamsize)len);
    return s;
}

std::vector<float> SemanticMemory::read_floats(std::istream& i,
                                                uint32_t n) {
    std::vector<float> v(n);
    i.read(reinterpret_cast<char*>(v.data()),
           (std::streamsize)(n * sizeof(float)));
    return v;
}

} // namespace axon
