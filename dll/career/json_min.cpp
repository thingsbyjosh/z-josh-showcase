#include "json_min.h"
#include "paths.h"   // mojibake fix 2026-07-22: FOpen (UTF-8 paths -> _wfopen)
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace jsonm {

// Value accessors ----
const Value* Value::member(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    auto it = obj.find(key);
    return it != obj.end() ? it->second.get() : nullptr;
}
bool Value::has(const std::string& key) const {
    const Value* v = member(key);
    return v != nullptr && v->type != Type::Null;
}
std::string Value::memberString(const std::string& key, const std::string& def) const {
    const Value* v = member(key);
    return (v && v->type == Type::String) ? v->str : def;
}
double Value::memberNumber(const std::string& key, double def) const {
    const Value* v = member(key);
    return (v && v->type == Type::Number) ? v->num : def;
}
int Value::memberInt(const std::string& key, int def) const {
    const Value* v = member(key);
    return (v && v->type == Type::Number) ? (int)v->num : def;
}
long Value::memberLong(const std::string& key, long def) const {
    const Value* v = member(key);
    return (v && v->type == Type::Number) ? (long)v->num : def;
}
bool Value::memberBool(const std::string& key, bool def) const {
    const Value* v = member(key);
    return (v && v->type == Type::Bool) ? v->b : def;
}

// recursive-descent parser ----
namespace {

struct Parser {
    const char* p;
    const char* end;
    std::string err;
    bool ok = true;

    Parser(const std::string& s) : p(s.c_str()), end(s.c_str() + s.size()) {}

    void fail(const char* msg) {
        if (ok) { ok = false; err = msg; }
    }

    void skipWs() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++p;
            else break;
        }
    }

    // appends a code point (from a \uXXXX escape) as UTF-8. The data doesn't use \u,
    // but it's supported defensively so unexpected input doesn't break things.
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp <= 0x7F) {
            out += (char)cp;
        } else if (cp <= 0x7FF) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    int hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= end) { fail("string: incomplete \\u"); return -1; }
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else { fail("string: invalid hex digit"); return -1; }
        }
        return (int)v;
    }

    // reads a string (assumes *p == '"'). Passes UTF-8 bytes verbatim; processes escapes.
    std::string parseString() {
        std::string out;
        if (p >= end || *p != '"') { fail("expected '\"'"); return out; }
        ++p;  // opening quote
        while (p < end) {
            unsigned char c = (unsigned char)*p++;
            if (c == '"') return out;                       // closing
            if (c == '\\') {
                if (p >= end) { fail("escape at end of input"); return out; }
                char e = *p++;
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        int cp = hex4();
                        if (cp < 0) return out;
                        // high surrogate -> combine with the low one if it follows
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 1 < end && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            int lo = hex4();
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                unsigned full = 0x10000u + (((unsigned)cp - 0xD800u) << 10) + ((unsigned)lo - 0xDC00u);
                                appendUtf8(out, full);
                            } else {
                                appendUtf8(out, (unsigned)cp);     // no valid low surrogate
                                if (lo >= 0) appendUtf8(out, (unsigned)lo);
                            }
                        } else {
                            appendUtf8(out, (unsigned)cp);
                        }
                        break;
                    }
                    default:
                        // unknown escape: keeps the character as-is (defensive)
                        out += e;
                        break;
                }
            } else {
                out += (char)c;                              // byte verbatim (incl. UTF-8 multibyte)
            }
        }
        fail("unterminated string");
        return out;
    }

    ValuePtr parseNumber() {
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end) {
            char c = *p;
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') ++p;
            else break;
        }
        std::string tok(start, p);
        if (tok.empty()) { fail("empty number"); return nullptr; }
        auto v = std::make_shared<Value>();
        v->type = Type::Number;
        v->num = strtod(tok.c_str(), nullptr);
        return v;
    }

    ValuePtr parseValue() {
        skipWs();
        if (p >= end) { fail("unexpected end"); return nullptr; }
        char c = *p;
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': {
                auto v = std::make_shared<Value>();
                v->type = Type::String;
                v->str = parseString();
                return ok ? v : nullptr;
            }
            case 't':
                if (end - p >= 4 && strncmp(p, "true", 4) == 0) {
                    p += 4; auto v = std::make_shared<Value>(); v->type = Type::Bool; v->b = true; return v;
                }
                fail("invalid literal (true?)"); return nullptr;
            case 'f':
                if (end - p >= 5 && strncmp(p, "false", 5) == 0) {
                    p += 5; auto v = std::make_shared<Value>(); v->type = Type::Bool; v->b = false; return v;
                }
                fail("invalid literal (false?)"); return nullptr;
            case 'n':
                if (end - p >= 4 && strncmp(p, "null", 4) == 0) {
                    p += 4; auto v = std::make_shared<Value>(); v->type = Type::Null; return v;
                }
                fail("invalid literal (null?)"); return nullptr;
            default:
                if (c == '-' || c == '+' || (c >= '0' && c <= '9')) return parseNumber();
                fail("unexpected token"); return nullptr;
        }
    }

    ValuePtr parseObject() {
        auto v = std::make_shared<Value>();
        v->type = Type::Object;
        ++p;  // '{'
        skipWs();
        if (p < end && *p == '}') { ++p; return v; }
        while (p < end && ok) {
            skipWs();
            if (p >= end || *p != '"') { fail("expected string key"); return nullptr; }
            std::string key = parseString();
            if (!ok) return nullptr;
            skipWs();
            if (p >= end || *p != ':') { fail("expected ':'"); return nullptr; }
            ++p;  // ':'
            ValuePtr val = parseValue();
            if (!ok) return nullptr;
            v->obj[key] = val;                               // last one wins on duplicate
            skipWs();
            if (p >= end) { fail("unterminated object"); return nullptr; }
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return v; }
            fail("expected ',' or '}'"); return nullptr;
        }
        fail("unterminated object");
        return nullptr;
    }

    ValuePtr parseArray() {
        auto v = std::make_shared<Value>();
        v->type = Type::Array;
        ++p;  // '['
        skipWs();
        if (p < end && *p == ']') { ++p; return v; }
        while (p < end && ok) {
            ValuePtr el = parseValue();
            if (!ok) return nullptr;
            v->arr.push_back(el);
            skipWs();
            if (p >= end) { fail("unterminated array"); return nullptr; }
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return v; }
            fail("expected ',' or ']'"); return nullptr;
        }
        fail("unterminated array");
        return nullptr;
    }
};

// strips a leading UTF-8 BOM if present
void stripBom(std::string& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
}

} // anon namespace

ValuePtr Parse(const std::string& textIn, std::string* errOut) {
    std::string text = textIn;
    stripBom(text);
    Parser ps(text);
    ValuePtr root = ps.parseValue();
    if (ps.ok) {
        ps.skipWs();
        if (ps.p != ps.end) ps.fail("trailing garbage after root value");
    }
    if (!ps.ok) {
        if (errOut) *errOut = ps.err;
        return nullptr;
    }
    return root;
}

ValuePtr ParseFile(const char* path, std::string* errOut) {
    // mojibake fix 2026-07-22: the path arrives in UTF-8 (config.json/paths) -> open via the wide API
    FILE* f = paths::FOpen(path, "rb");
    if (!f) { if (errOut) *errOut = "cannot open file"; return nullptr; }
    std::string data;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    return Parse(data, errOut);
}

} // namespace jsonm
