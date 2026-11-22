/**
 * runtime/include/lexer.h
 * Axon language lexer — hand-written C++17
 */
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include "platform.h"

namespace axon {

enum class TK : uint8_t {
    NUMBER, STRING, IDENT, MODEL_IDENT, RAW_CODE,
    KW_IMPORT, KW_AGENT, KW_FLOW, KW_GRAPH, KW_ROLE,
    KW_EMBEDDING, KW_TASK, KW_MAIN,
    KW_PROVIDER, KW_MODEL, KW_TEMPERATURE, KW_SESSION,
    KW_VERSION, KW_TELEMETRY, KW_INPUT, KW_OUTPUT, KW_PROMPTS,
    KW_MEMORY, KW_STORAGE, KW_VALIDATION, KW_LEARNING, KW_QUANTIZATION,
    KW_RUNTIME, KW_SEMANTIC, KW_PERSISTENT, KW_SHARED, KW_TEMPORARY,
    KW_CONFIDENCE, KW_FORMAT, KW_MAX_RETRIES, KW_ON_FAIL, KW_ON_SUCCESS,
    KW_RETRY, KW_FLAG, KW_SKIP, KW_RAISE,
    KW_METHOD, KW_CAPTURE, KW_THRESHOLD, KW_COMPRESS, KW_APPLY,
    KW_RAG, KW_LORA, KW_IMPRINTING,
    KW_DONE, KW_CONDITION,
    KW_DIM, KW_INDEX, KW_PERSIST, KW_HNSW, KW_FLAT, KW_IVF, KW_BITS,
    KW_IF, KW_ELSE, KW_FOR, KW_IN, KW_WHILE, KW_MATCH,
    KW_TRY, KW_CATCH, KW_RETURN, KW_FUNC,
    KW_PYTHON, KW_SQL, KW_SHELL, KW_CPP,
    KW_TRUE, KW_FALSE, KW_NULL, KW_ENABLED, KW_DISABLED, KW_TOP_K,
    KW_AND, KW_OR, KW_NOT,
    LBRACE, RBRACE, LPAREN, RPAREN, LBRACKET, RBRACKET,
    COMMA, DOT, COLON, QUESTION, WILDCARD,
    ASSIGN, ARROW, FAT_ARROW, EQ, NEQ, LT, GT, LTE, GTE,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    // ── Task graph & I/O ────────────────────────────────
    KW_TASK_GRAPH,   // task_graph
    KW_IO,           // io
    KW_RECEIVE,      // receive
    KW_EMIT,         // emit
    KW_RUN,          // run
    KW_GRPC,         // grpc
    KW_SOCKET,       // socket
    KW_ULTRASONIC,   // ultrasonic
    KW_NEXT_TASK,    // next_task
    // ── Transport & output config ────────────────────────
    KW_TRANSPORT,    // transport
    KW_ENDPOINT,     // endpoint
    // ── Parallel execution ────────────────────────────────
    KW_PARALLEL,     // parallel
    KW_SPAWN,        // spawn
    KW_AWAIT,          // await
    PIPELINE,          // >>
    POWER,             // **
    FSTRING,           // f"..." interpolated string
    KW_TYPEOF,         // typeof
    AND_OP,            // && alias for and
    OR_OP,             // || alias for or
    END_OF_FILE, UNKNOWN
};

struct Token {
    TK          type;
    std::string value;
    uint32_t    line = 1;
    uint32_t    col  = 1;
    bool is(TK t) const { return type == t; }
    bool is_interop_lang() const {
        return type==TK::KW_PYTHON||type==TK::KW_SQL
            ||type==TK::KW_SHELL||type==TK::KW_CPP;
    }
    bool is_memory_type() const {
        return type==TK::KW_RUNTIME||type==TK::KW_SESSION
            ||type==TK::KW_SEMANTIC||type==TK::KW_PERSISTENT
            ||type==TK::KW_SHARED||type==TK::KW_TEMPORARY;
    }
    std::string type_name() const;
};

struct LexError {
    std::string message;
    uint32_t line, col;
};

class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<input>");
    std::vector<Token> tokenize();
    // Called by parser to read raw interop block content
    // (call after consuming the opening '{')
    Token read_raw_block();
    const std::vector<LexError>& errors() const { return _errors; }
    bool has_errors() const { return !_errors.empty(); }

private:
    char  peek(int offset = 0) const;
    char  advance();
    bool  at_end() const;
    void  skip_ws_and_comments();
    Token read_next();
    Token read_number();
    Token read_string();
    Token read_triple_string();
    Token read_ident_or_kw();
    Token make(TK t, std::string v = "");
    void  push_error(const std::string& msg);
    static const std::unordered_map<std::string,TK>& kw_map();

    std::string           _source;
    std::string           _filename;
    size_t                _pos  = 0;
    uint32_t              _line = 1;
    uint32_t              _col  = 1;
    std::optional<Token>  _stash;          // one-token lookahead buffer
    std::vector<LexError> _errors;
};

} // namespace axon
