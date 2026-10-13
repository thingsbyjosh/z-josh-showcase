#include "json_write.h"
#include <cmath>
#include <cstdio>

namespace jsonw {

std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;   // verbatim UTF-8
                }
        }
    }
    return out;
}

void Writer::comma() {
    if (pendingKey_) { pendingKey_ = false; return; }  // value after a key: no comma
    if (first_.empty()) return;                        // root value
    if (first_.back()) first_.back() = false;
    else out_ += ',';
}

void Writer::BeginObject() { comma(); out_ += '{'; first_.push_back(true); }
void Writer::EndObject()   { out_ += '}'; first_.pop_back(); }
void Writer::BeginArray()  { comma(); out_ += '['; first_.push_back(true); }
void Writer::EndArray()    { out_ += ']'; first_.pop_back(); }

void Writer::Key(const char* k) {
    if (first_.back()) first_.back() = false;
    else out_ += ',';
    out_ += '"';
    out_ += Escape(k);
    out_ += "\":";
    pendingKey_ = true;
}

void Writer::S(const std::string& v) { comma(); out_ += '"'; out_ += Escape(v); out_ += '"'; }

void Writer::N(double v) {
    comma();
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 9.007199254740992e15) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)v);
        out_ += buf;
    } else if (std::isfinite(v)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.17g", v);
        out_ += buf;
    } else {
        out_ += "null";   // NaN/Inf don't exist in JSON
    }
}

void Writer::I(long long v) {
    comma();
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    out_ += buf;
}

void Writer::B(bool v) { comma(); out_ += v ? "true" : "false"; }
void Writer::Null()    { comma(); out_ += "null"; }
void Writer::Raw(const std::string& json) { comma(); out_ += json; }

} // namespace jsonw
