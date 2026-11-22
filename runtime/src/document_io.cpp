/**
 * runtime/src/document_io.cpp
 * ────────────────────────────
 * Native CSV + JSON parsers. No external dependencies.
 */
#include "../include/document_io.h"
#include "../include/interpreter.h"

#include <sstream>
#include <fstream>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace axon {

// ─────────────────────────────────────────────────────────────
// File type detection
// ─────────────────────────────────────────────────────────────
FileType detect_file_type(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return FileType::Text;

    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);

    if (ext == "csv" || ext == "tsv")    return FileType::CSV;
    if (ext == "json" || ext == "jsonl") return FileType::JSON;
    if (ext == "pdf")                    return FileType::PDF;
    if (ext == "docx" || ext == "doc")   return FileType::DOCX;
    if (ext == "xlsx" || ext == "xls")   return FileType::XLSX;
    if (ext == "jpg"  || ext == "jpeg"
     || ext == "png"  || ext == "bmp"
     || ext == "gif"  || ext == "webp")  return FileType::Image;
    if (ext == "wav"  || ext == "mp3"
     || ext == "flac" || ext == "ogg"
     || ext == "m4a")                    return FileType::Audio;
    if (ext == "txt"  || ext == "md"
     || ext == "log"  || ext == "yaml"
     || ext == "toml" || ext == "ini")   return FileType::Text;
    return FileType::Text;
}

// ─────────────────────────────────────────────────────────────
// CSV parser (RFC 4180)
// ─────────────────────────────────────────────────────────────
static std::vector<std::string> csv_parse_row(const std::string& line,
                                               char delim) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (in_quotes) {
            if (c == '"') {
                // Check for escaped quote ""
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == delim) {
                fields.push_back(field);
                field.clear();
            } else if (c == '\r') {
                // Skip carriage return (Windows line endings)
            } else {
                field += c;
            }
        }
    }
    fields.push_back(field);
    return fields;
}

Value csv_parse(const std::string& text, char delimiter, bool has_header) {
    Value result;
    result.kind = Value::Kind::List;

    if (text.empty()) return result;

    std::istringstream ss(text);
    std::string line;

    std::vector<std::string> headers;
    bool first_line = true;

    while (std::getline(ss, line)) {
        // Strip trailing \r if present
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        auto fields = csv_parse_row(line, delimiter);

        if (first_line) {
            first_line = false;
            if (has_header) {
                headers = fields;
                continue;
            } else {
                // Generate column names: col0, col1, col2...
                for (size_t i = 0; i < fields.size(); ++i)
                    headers.push_back("col" + std::to_string(i));
            }
        }

        // Build dict for this row
        Value row;
        row.kind = Value::Kind::Dict;
        for (size_t i = 0; i < fields.size(); ++i) {
            std::string key = (i < headers.size())
                              ? headers[i]
                              : "col" + std::to_string(i);
            const std::string& val = fields[i];

            // Try to parse as number
            bool is_num = false;
            if (!val.empty()) {
                try {
                    size_t pos;
                    double n = std::stod(val, &pos);
                    if (pos == val.size() && !std::isnan(n)) {
                        row.dict[key] = Value::from(n);
                        is_num = true;
                    }
                } catch (...) {}
            }
            if (!is_num) {
                // bool check
                if (val == "true" || val == "TRUE")
                    row.dict[key] = Value::from(true);
                else if (val == "false" || val == "FALSE")
                    row.dict[key] = Value::from(false);
                else
                    row.dict[key] = Value::from(val);
            }
        }
        result.list.push_back(std::move(row));
    }

    return result;
}

std::string csv_stringify(const Value& data, char delimiter) {
    std::ostringstream out;
    std::string delim(1, delimiter);

    if (data.kind == Value::Kind::List && !data.list.empty()) {
        // Collect all keys for header
        std::vector<std::string> headers;
        if (data.list[0].kind == Value::Kind::Dict) {
            for (auto& [k, _] : data.list[0].dict)
                headers.push_back(k);
            std::sort(headers.begin(), headers.end());

            // Write header
            for (size_t i = 0; i < headers.size(); ++i) {
                if (i) out << delimiter;
                out << headers[i];
            }
            out << "\n";

            // Write rows
            for (auto& row : data.list) {
                for (size_t i = 0; i < headers.size(); ++i) {
                    if (i) out << delimiter;
                    auto it = row.dict.find(headers[i]);
                    if (it != row.dict.end()) {
                        const Value& v = it->second;
                        if (v.kind == Value::Kind::String) {
                            // Quote if contains delimiter or newline
                            bool need_quote = v.s.find(delimiter) != std::string::npos
                                           || v.s.find('\n') != std::string::npos
                                           || v.s.find('"')  != std::string::npos;
                            if (need_quote) {
                                out << '"';
                                for (char c : v.s) {
                                    if (c == '"') out << '"'; // escape
                                    out << c;
                                }
                                out << '"';
                            } else {
                                out << v.s;
                            }
                        } else if (v.kind == Value::Kind::Number) {
                            if (v.n == (long long)v.n)
                                out << (long long)v.n;
                            else
                                out << v.n;
                        } else if (v.kind == Value::Kind::Bool) {
                            out << (v.b ? "true" : "false");
                        }
                    }
                }
                out << "\n";
            }
        } else {
            // List of lists
            for (auto& row : data.list) {
                if (row.kind == Value::Kind::List) {
                    for (size_t i = 0; i < row.list.size(); ++i) {
                        if (i) out << delimiter;
                        const Value& v = row.list[i];
                        if (v.kind == Value::Kind::String) out << v.s;
                        else if (v.kind == Value::Kind::Number) {
                            if (v.n == (long long)v.n) out << (long long)v.n;
                            else out << v.n;
                        }
                    }
                    out << "\n";
                }
            }
        }
    }
    return out.str();
}

// ─────────────────────────────────────────────────────────────
// JSON parser
// ─────────────────────────────────────────────────────────────
struct JsonParser {
    const std::string& src;
    size_t pos = 0;

    JsonParser(const std::string& s) : src(s) {}

    void skip_ws() {
        while (pos < src.size() && (src[pos]==' '||src[pos]=='\t'
                                   ||src[pos]=='\n'||src[pos]=='\r'))
            ++pos;
    }

    char peek() { return pos < src.size() ? src[pos] : '\0'; }
    char next() { return pos < src.size() ? src[pos++] : '\0'; }

    Value parse_value() {
        skip_ws();
        if (pos >= src.size()) return Value::null();

        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string_val();
        if (c == 't') return parse_true();
        if (c == 'f') return parse_false();
        if (c == 'n') return parse_null();
        if (c == '-' || isdigit((unsigned char)c)) return parse_number();
        return Value::null();
    }

    Value parse_object() {
        Value obj; obj.kind = Value::Kind::Dict;
        ++pos; // consume '{'
        skip_ws();
        if (peek() == '}') { ++pos; return obj; }

        while (pos < src.size()) {
            skip_ws();
            if (peek() != '"') break;
            std::string key = parse_string_raw();
            skip_ws();
            if (peek() == ':') ++pos;
            skip_ws();
            Value val = parse_value();
            obj.dict[key] = std::move(val);
            skip_ws();
            if (peek() == ',') { ++pos; continue; }
            if (peek() == '}') { ++pos; break; }
            break;
        }
        return obj;
    }

    Value parse_array() {
        Value arr; arr.kind = Value::Kind::List;
        ++pos; // consume '['
        skip_ws();
        if (peek() == ']') { ++pos; return arr; }

        while (pos < src.size()) {
            skip_ws();
            arr.list.push_back(parse_value());
            skip_ws();
            if (peek() == ',') { ++pos; continue; }
            if (peek() == ']') { ++pos; break; }
            break;
        }
        return arr;
    }

    std::string parse_string_raw() {
        if (peek() != '"') return "";
        ++pos; // consume opening "
        std::string result;
        while (pos < src.size() && peek() != '"') {
            char c = next();
            if (c == '\\' && pos < src.size()) {
                char esc = next();
                switch (esc) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        // \uXXXX — basic ASCII range only
                        std::string hex;
                        for (int i = 0; i < 4 && pos < src.size(); ++i)
                            hex += next();
                        try {
                            int code = std::stoi(hex, nullptr, 16);
                            if (code < 128) result += (char)code;
                        } catch (...) {}
                        break;
                    }
                    default: result += esc;
                }
            } else {
                result += c;
            }
        }
        if (peek() == '"') ++pos; // consume closing "
        return result;
    }

    Value parse_string_val() {
        return Value::from(parse_string_raw());
    }

    Value parse_number() {
        size_t start = pos;
        if (peek() == '-') ++pos;
        while (pos < src.size() && (isdigit((unsigned char)src[pos]) || src[pos] == '.'
               || src[pos] == 'e' || src[pos] == 'E'
               || src[pos] == '+' || src[pos] == '-'))
            ++pos;
        try {
            double n = std::stod(src.substr(start, pos - start));
            return Value::from(n);
        } catch (...) { return Value::null(); }
    }

    Value parse_true()  {
        pos += 4; // "true"
        return Value::from(true);
    }
    Value parse_false() {
        pos += 5; // "false"
        return Value::from(false);
    }
    Value parse_null()  {
        pos += 4; // "null"
        return Value::null();
    }
};

Value json_parse(const std::string& text) {
    if (text.empty()) return Value::null();
    JsonParser p(text);
    return p.parse_value();
}

// ─────────────────────────────────────────────────────────────
// JSON serializer
// ─────────────────────────────────────────────────────────────
static std::string json_escape_str(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

static void json_write(const Value& v, std::ostringstream& out,
                       int indent, int depth) {
    std::string nl    = indent > 0 ? "\n" : "";
    std::string space = indent > 0 ? std::string(indent, ' ') : "";
    std::string pad   = indent > 0 ? std::string(depth * indent, ' ') : "";
    std::string inner = indent > 0 ? std::string((depth+1) * indent, ' ') : "";

    switch (v.kind) {
        case Value::Kind::Null:   out << "null";  break;
        case Value::Kind::Bool:   out << (v.b ? "true" : "false"); break;
        case Value::Kind::Number:
            if (v.n == (long long)v.n) out << (long long)v.n;
            else {
                std::ostringstream ns; ns << v.n;
                out << ns.str();
            }
            break;
        case Value::Kind::String:
            out << '"' << json_escape_str(v.s) << '"';
            break;
        case Value::Kind::List:
            out << '[';
            if (!v.list.empty()) {
                out << nl;
                for (size_t i = 0; i < v.list.size(); ++i) {
                    out << inner;
                    json_write(v.list[i], out, indent, depth + 1);
                    if (i + 1 < v.list.size()) out << ',';
                    out << nl;
                }
                out << pad;
            }
            out << ']';
            break;
        case Value::Kind::Dict: {
            out << '{';
            if (!v.dict.empty()) {
                out << nl;
                // Sort keys for deterministic output
                std::vector<std::string> keys;
                for (auto& [k, _] : v.dict) keys.push_back(k);
                std::sort(keys.begin(), keys.end());
                for (size_t i = 0; i < keys.size(); ++i) {
                    out << inner << '"' << json_escape_str(keys[i]) << '"';
                    out << (indent > 0 ? ": " : ":");
                    json_write(v.dict.at(keys[i]), out, indent, depth + 1);
                    if (i + 1 < keys.size()) out << ',';
                    out << nl;
                }
                out << pad;
            }
            out << '}';
            break;
        }
        default: out << "null"; break;
    }
}

std::string json_stringify(const Value& v, int indent) {
    std::ostringstream out;
    json_write(v, out, indent, 0);
    return out.str();
}

} // namespace axon
