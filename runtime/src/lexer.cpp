/**
 * runtime/src/lexer.cpp — Axon Lexer implementation
 */
#include "lexer.h"
#include <cctype>
#include <sstream>

namespace axon {

// ── Keyword map ──────────────────────────────────────────────
const std::unordered_map<std::string,TK>& Lexer::kw_map() {
    static const std::unordered_map<std::string,TK> m = {
        {"import",TK::KW_IMPORT},{"agent",TK::KW_AGENT},
        {"flow",TK::KW_FLOW},{"graph",TK::KW_GRAPH},
        {"role",TK::KW_ROLE},{"embedding",TK::KW_EMBEDDING},
        {"task",TK::KW_TASK},{"main",TK::KW_MAIN},
        {"provider",TK::KW_PROVIDER},{"model",TK::KW_MODEL},
        {"temperature",TK::KW_TEMPERATURE},{"session",TK::KW_SESSION},
        {"version",TK::KW_VERSION},{"telemetry",TK::KW_TELEMETRY},
        {"input",TK::KW_INPUT},{"output",TK::KW_OUTPUT},
        {"prompts",TK::KW_PROMPTS},
        {"memory",TK::KW_MEMORY},{"storage",TK::KW_STORAGE},
        {"validation",TK::KW_VALIDATION},{"learning",TK::KW_LEARNING},
        {"quantization",TK::KW_QUANTIZATION},
        {"runtime",TK::KW_RUNTIME},{"semantic",TK::KW_SEMANTIC},
        {"persistent",TK::KW_PERSISTENT},{"shared",TK::KW_SHARED},
        {"temporary",TK::KW_TEMPORARY},
        {"confidence",TK::KW_CONFIDENCE},{"format",TK::KW_FORMAT},
        {"max_retries",TK::KW_MAX_RETRIES},{"on_fail",TK::KW_ON_FAIL},
        {"on_success",TK::KW_ON_SUCCESS},
        {"retry",TK::KW_RETRY},{"flag",TK::KW_FLAG},
        {"skip",TK::KW_SKIP},{"raise",TK::KW_RAISE},
        {"method",TK::KW_METHOD},{"capture",TK::KW_CAPTURE},
        {"threshold",TK::KW_THRESHOLD},{"compress",TK::KW_COMPRESS},
        {"apply",TK::KW_APPLY},{"rag",TK::KW_RAG},
        {"lora",TK::KW_LORA},{"imprinting",TK::KW_IMPRINTING},
        {"done",TK::KW_DONE},{"condition",TK::KW_CONDITION},
        {"dim",TK::KW_DIM},{"index",TK::KW_INDEX},
        {"persist",TK::KW_PERSIST},{"hnsw",TK::KW_HNSW},
        {"flat",TK::KW_FLAT},{"ivf",TK::KW_IVF},{"bits",TK::KW_BITS},
        {"if",TK::KW_IF},{"else",TK::KW_ELSE},
        {"for",TK::KW_FOR},{"in",TK::KW_IN},
        {"while",TK::KW_WHILE},{"match",TK::KW_MATCH},
        {"try",TK::KW_TRY},{"catch",TK::KW_CATCH},
        {"return",TK::KW_RETURN},
        {"func",TK::KW_FUNC},
        {"python",TK::KW_PYTHON},{"sql",TK::KW_SQL},
        {"shell",TK::KW_SHELL},{"cpp",TK::KW_CPP},
        {"true",TK::KW_TRUE},{"false",TK::KW_FALSE},
        {"null",TK::KW_NULL},{"enabled",TK::KW_ENABLED},
        {"disabled",TK::KW_DISABLED},{"top_k",TK::KW_TOP_K},
        {"and",TK::KW_AND},{"or",TK::KW_OR},{"not",TK::KW_NOT},
        // task graph & I/O
        {"task_graph",TK::KW_TASK_GRAPH},{"io",TK::KW_IO},
        {"receive",TK::KW_RECEIVE},{"emit",TK::KW_EMIT},
        {"run",TK::KW_RUN},{"grpc",TK::KW_GRPC},
        {"socket",TK::KW_SOCKET},{"ultrasonic",TK::KW_ULTRASONIC},
        {"next_task",TK::KW_NEXT_TASK},{"typeof",TK::KW_TYPEOF},
        // transport & output config
        {"transport",TK::KW_TRANSPORT},{"endpoint",TK::KW_ENDPOINT},
        // parallel
        {"parallel",TK::KW_PARALLEL},{"spawn",TK::KW_SPAWN},
    };
    return m;
}

std::string Token::type_name() const {
    switch(type){
        case TK::NUMBER:return "NUMBER"; case TK::STRING:return "STRING";
        case TK::IDENT:return "IDENT"; case TK::MODEL_IDENT:return "MODEL_IDENT";
        case TK::RAW_CODE:return "RAW_CODE";
        case TK::LBRACE:return "{"; case TK::RBRACE:return "}";
        case TK::LPAREN:return "("; case TK::RPAREN:return ")";
        case TK::LBRACKET:return "["; case TK::RBRACKET:return "]";
        case TK::COMMA:return ","; case TK::DOT:return ".";
        case TK::COLON:return ":"; case TK::QUESTION:return "?";
        case TK::WILDCARD:return "_"; case TK::ASSIGN:return "=";
        case TK::ARROW:return "->"; case TK::FAT_ARROW:return "=>"; case TK::EQ:return "==";
        case TK::NEQ:return "!="; case TK::LT:return "<";
        case TK::GT:return ">"; case TK::LTE:return "<=";
        case TK::GTE:return ">="; case TK::PLUS:return "+";
        case TK::MINUS:return "-"; case TK::STAR:return "*";
        case TK::SLASH:return "/"; case TK::PERCENT:return "%";
        case TK::PIPELINE:return ">>"; case TK::POWER:return "**"; case TK::FSTRING:return "FSTRING"; case TK::AND_OP:return "&&"; case TK::OR_OP:return "||"; case TK::END_OF_FILE:return "EOF";
        default: return "KW(" + value + ")";
    }
}

Lexer::Lexer(std::string source, std::string filename)
    : _source(std::move(source)), _filename(std::move(filename)) {}

char Lexer::peek(int off) const {
    size_t i = _pos + (size_t)off;
    return i < _source.size() ? _source[i] : '\0';
}
char Lexer::advance() {
    char c = _source[_pos++];
    if (c=='\n'){++_line;_col=1;}else{++_col;}
    return c;
}
bool Lexer::at_end() const { return _pos >= _source.size(); }

void Lexer::skip_ws_and_comments() {
    while (!at_end()) {
        char c = peek();
        if (c==' '||c=='\t'||c=='\r'||c=='\n') { advance(); }
        else if (c=='#') { while(!at_end()&&peek()!='\n') advance(); }
        else break;
    }
}

void Lexer::push_error(const std::string& msg) {
    _errors.push_back({msg, _line, _col});
}

Token Lexer::make(TK t, std::string v) {
    return Token{t, std::move(v), _line, _col};
}

// ── Main tokenize loop ───────────────────────────────────────
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        if (_stash) {
            tokens.push_back(*_stash);
            _stash.reset();
        }
        skip_ws_and_comments();
        if (at_end()) { tokens.push_back(make(TK::END_OF_FILE)); break; }

        Token tok = read_next();

        // For interop keywords: read the { ... } block as RAW text,
        // not as individual tokens. This preserves indentation and syntax.
        if (tok.is_interop_lang()) {
            tokens.push_back(tok);
            skip_ws_and_comments();
            if (tok.type == TK::KW_PYTHON && peek() == '[') {
                int brackets = 0;
                do {
                    skip_ws_and_comments();
                    if (at_end()) { push_error("unterminated Python parameter list"); break; }
                    Token parameter = read_next();
                    if (parameter.type == TK::LBRACKET) ++brackets;
                    if (parameter.type == TK::RBRACKET) --brackets;
                    tokens.push_back(parameter);
                } while (brackets > 0);
                skip_ws_and_comments();
            }
            if (!at_end() && peek() == '{') {
                advance();                              // consume '{'
                tokens.push_back(make(TK::LBRACE, "{"));
                tokens.push_back(read_raw_block(tok.type));
                tokens.push_back(make(TK::RBRACE, "}"));
            }
            continue;
        }

        tokens.push_back(tok);
    }
    return tokens;
}

// ── read_next ────────────────────────────────────────────────
Token Lexer::read_next() {
    char c = peek();
    if (std::isdigit((unsigned char)c))            return read_number();
    if (c=='"') {
        if (peek(1)=='"'&&peek(2)=='"') return read_triple_string();
        return read_string();
    }
    // Single-quoted string
    if (c == (char)39) {  // 39 = single quote '
        std::string val;
        advance(); // consume opening quote
        while (!at_end() && peek() != (char)39) {
            char ch = advance();
            if (ch == (char)92 && !at_end()) {  // 92 = backslash
                char esc = advance();
                if   (esc == 'n')          val += (char)10;  // newline
                else if (esc == 't')       val += (char)9;   // tab
                else if (esc == (char)39)  val += (char)39;  // single quote
                else if (esc == (char)92)  val += (char)92;  // backslash
                else { val += ch; val += esc; }
            } else {
                val += ch;
            }
        }
        if (!at_end()) advance(); // consume closing quote
        return make(TK::STRING, val);
    }
        if (std::isalpha((unsigned char)c)||c=='_')    return read_ident_or_kw();

    advance();
    switch (c) {
        case '{': return make(TK::LBRACE,"{");
        case '}': return make(TK::RBRACE,"}");
        case '(': return make(TK::LPAREN,"(");
        case ')': return make(TK::RPAREN,")");
        case '[': return make(TK::LBRACKET,"[");
        case ']': return make(TK::RBRACKET,"]");
        case ',': return make(TK::COMMA,",");
        case '.': return make(TK::DOT,".");
        case ':': return make(TK::COLON,":");
        case '?': return make(TK::QUESTION,"?");
        case '+': return make(TK::PLUS,"+");
        case '*':
            if(peek()=='*'){ advance(); return make(TK::POWER,"**"); }
            return make(TK::STAR,"*");
        case '/':
            // Check for block comment /* ... */
            if (peek() == '*') {
                // consume the block comment and return next real token
                advance(); // consume *
                while (!at_end()) {
                    if (peek()=='*' && peek(1)=='/') {
                        advance(); advance(); break;
                    }
                    advance();
                }
                // recurse to get the next real token
                skip_ws_and_comments();
                if (at_end()) return make(TK::END_OF_FILE);
                return read_next();
            }
            return make(TK::SLASH,"/");
        case '%': return make(TK::PERCENT,"%");
        case '&':
            if(peek()=='&'){ advance(); return make(TK::KW_AND,"and"); }
            push_error("unexpected '&'"); return make(TK::UNKNOWN,"&");
        case '|':
            if(peek()=='|'){ advance(); return make(TK::KW_OR,"or"); }
            push_error("unexpected '|'"); return make(TK::UNKNOWN,"|");
        case '=':
            if(peek()=='='){ advance(); return make(TK::EQ,"=="); }
            if(peek()=='>'){ advance(); return make(TK::FAT_ARROW,"=>"); }
            return make(TK::ASSIGN,"=");
        case '!':
            if(peek()=='='){ advance(); return make(TK::NEQ,"!="); }
            push_error("unexpected '!'"); return make(TK::UNKNOWN,"!");
        case '<':
            if(peek()=='='){ advance(); return make(TK::LTE,"<="); }
            return make(TK::LT,"<");
        case '>':
            if(peek()=='='){ advance(); return make(TK::GTE,">="); }
            if(peek()=='>'){ advance(); return make(TK::PIPELINE,">>"); }
            return make(TK::GT,">");
        case '-':
            if(peek()=='>'){ advance(); return make(TK::ARROW,"->"); }
            return make(TK::MINUS,"-");
        default:
            push_error(std::string("unexpected char '")+c+"'");
            return make(TK::UNKNOWN, std::string(1,c));
    }
}

Token Lexer::read_number() {
    std::string v;
    while (!at_end()&&std::isdigit((unsigned char)peek())) v+=advance();
    if (!at_end()&&peek()=='.'&&std::isdigit((unsigned char)peek(1))) {
        v+=advance();
        while (!at_end()&&std::isdigit((unsigned char)peek())) v+=advance();
    }
    return make(TK::NUMBER, v);
}

Token Lexer::read_string() {
    advance(); // opening "
    std::string v;
    while (!at_end()&&peek()!='"') {
        char c=advance();
        if (c=='\\') {
            char e=advance();
            switch(e){case 'n':v+='\n';break;case 't':v+='\t';break;
                      case '"':v+='"';break;case '\\':v+='\\';break;
                      default:v+='\\';v+=e;}
        } else v+=c;
    }
    if (at_end()) push_error("unterminated string");
    else          advance(); // closing "
    return make(TK::STRING, v);
}

Token Lexer::read_triple_string() {
    advance();advance();advance(); // """
    std::string v;
    while (!at_end()) {
        if (peek()=='"'&&peek(1)=='"'&&peek(2)=='"') {
            advance();advance();advance();
            if (!v.empty()&&v.front()=='\n') v.erase(0,1);
            if (!v.empty()&&v.back()=='\n')  v.pop_back();
            return make(TK::STRING, v);
        }
        v+=advance();
    }
    push_error("unterminated triple string");
    return make(TK::STRING, v);
}

Token Lexer::read_ident_or_kw() {
    std::string v;
    while (!at_end()&&(std::isalnum((unsigned char)peek())||peek()=='_')) v+=advance();

    if (v=="_") return make(TK::WILDCARD,"_");

    // f-string: f"..." or f'...'
    if (v == "f" && !at_end() && (peek() == '"' || peek() == (char)39)) {
        if (peek() == '"') {
            if (peek(1) == '"' && peek(2) == '"') {
                Token t = read_triple_string();
                t.type = TK::FSTRING;
                return t;
            }
            Token t = read_string();
            t.type = TK::FSTRING;
            return t;
        } else {
            // Single-quoted f-string: f'...'
            // Read as single-quoted string then mark as FSTRING
            std::string val;
            advance(); // consume opening single-quote
            while (!at_end() && peek() != (char)39) {
                char ch = advance();
                if (ch == (char)92 && !at_end()) {
                    char esc = advance();
                    if   (esc == 'n')         val += (char)10;
                    else if (esc == 't')      val += (char)9;
                    else if (esc == (char)39) val += (char)39;
                    else if (esc == (char)92) val += (char)92;
                    else { val += ch; val += esc; }
                } else {
                    val += ch;
                }
            }
            if (!at_end()) advance(); // consume closing single-quote
            return make(TK::FSTRING, val);
        }
    }

    const auto& m = kw_map();
    auto it = m.find(v);
    if (it != m.end()) {
        // After model/provider: read bare model name (may contain - . :)
        if (it->second==TK::KW_MODEL||it->second==TK::KW_PROVIDER) {
            Token kw_tok = make(it->second, v);
            // skip horizontal whitespace only
            while (!at_end()&&(peek()==' '||peek()=='\t')) advance();
            // if next char starts a bare word (not string/newline/brace)
            if (!at_end()&&peek()!='\n'&&peek()!='"'
                &&peek()!='{'&&peek()!='}'&&peek()!='#'
                &&(std::isalnum((unsigned char)peek())||peek()=='_')) {
                std::string mv;
                while (!at_end()&&(std::isalnum((unsigned char)peek())||peek()=='_'
                       ||peek()=='-'||peek()=='.'||peek()==':'))
                    mv+=advance();
                _stash = Token{TK::MODEL_IDENT, mv, _line, _col};
            }
            return kw_tok;
        }
        return make(it->second, v);
    }
    return make(TK::IDENT, v);
}

// Called by parser after consuming the interop keyword + '{'
Token Lexer::read_raw_block(TK language) {
    const auto start_line=_line, start_col=_col;
    std::string code;
    int depth = 1;
    while (!at_end()&&depth>0) {
        char c = peek();
        if (language == TK::KW_PYTHON || language == TK::KW_SHELL) {
            if (c == '#') {
                while (!at_end() && peek() != '\n') code += advance();
                continue;
            }
            if (c == '\'' || c == '"' || (language == TK::KW_SHELL && c == '`')) {
                const char quote = c;
                const bool triple = language == TK::KW_PYTHON && peek(1) == quote && peek(2) == quote;
                code += advance();
                if (triple) { code += advance(); code += advance(); }
                while (!at_end()) {
                    if (peek() == '\\') {
                        code += advance(); if (!at_end()) code += advance(); continue;
                    }
                    if (peek() == quote && (!triple || (peek(1) == quote && peek(2) == quote))) {
                        code += advance();
                        if (triple) { code += advance(); code += advance(); }
                        break;
                    }
                    code += advance();
                }
                continue;
            }
        }
        if (language == TK::KW_SQL) {
            // SQL literals, quoted identifiers and comments may contain unmatched braces.
            if (c == '\'' || c == '"' || c == '`' || c == '[') {
                const char close = c == '[' ? ']' : c;
                code += advance();
                while (!at_end()) {
                    const char ch = advance(); code += ch;
                    if (ch == close) {
                        if (peek() == close) { code += advance(); continue; }
                        break;
                    }
                }
                continue;
            }
            if (c == '-' && peek(1) == '-') {
                while (!at_end() && peek() != '\n') code += advance();
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                code += advance(); code += advance();
                while (!at_end()) {
                    if (peek() == '*' && peek(1) == '/') { code += advance(); code += advance(); break; }
                    code += advance();
                }
                continue;
            }
        }
        if (c=='{')      ++depth;
        else if (c=='}') { if(--depth==0) break; }
        code+=advance();
    }
    if (at_end()&&depth>0) push_error("unterminated interop block");
    else                   advance(); // consume '}'
    return Token{TK::RAW_CODE,code,start_line,start_col};
}

} // namespace axon
