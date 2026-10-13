// json_write -- a minimalist JSON writer (json_min's counterpart, which only parses).
// For career_api's responses. No dependencies; manages commas via a stack.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace jsonw {

// Escapes a string to embed it inside JSON quotes (\" \\ \n \r \t \b \f + control chars).
// UTF-8 bytes pass through verbatim (same criterion as json_min).
std::string Escape(const std::string& s);

class Writer {
public:
    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();
    void Key(const char* k);

    void S(const std::string& v);      // string value
    void N(double v);                  // numeric value (integer with no decimals if it is one)
    void I(long long v);
    void B(bool v);
    void Null();
    void Raw(const std::string& json); // embeds already-serialized JSON (handler payloads)

    // key+value convenience
    void KS(const char* k, const std::string& v) { Key(k); S(v); }
    void KN(const char* k, double v)             { Key(k); N(v); }
    void KI(const char* k, long long v)          { Key(k); I(v); }
    void KB(const char* k, bool v)               { Key(k); B(v); }
    void KRaw(const char* k, const std::string& json) { Key(k); Raw(json); }

    const std::string& Str() const { return out_; }

private:
    void comma();
    std::string out_;
    std::vector<bool> first_;   // per nesting level
    bool pendingKey_ = false;
};

} // namespace jsonw
