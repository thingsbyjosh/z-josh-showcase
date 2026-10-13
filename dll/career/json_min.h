#pragma once
// json_min -- a minimalist, dependency-free, recursive-descent JSON parser.
// For the career mode's season-JSON data model (P1b). The data is
// machine-generated: well-formed, UTF-8 with LITERAL accents (no \uXXXX),
// unique keys per object. The parser passes UTF-8 bytes verbatim (doesn't decode
// unicode escapes), defensively handles \" \\ \/ \n \t \r \b \f, and tolerates trailing
// whitespace. Objects: the last value of a duplicate key wins (shouldn't happen).
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace jsonm {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value;
using ValuePtr = std::shared_ptr<Value>;

struct Value {
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;                              // verbatim UTF-8 bytes
    std::vector<ValuePtr> arr;
    std::map<std::string, ValuePtr> obj;          // sorted by key (enough for key-based access)

    // safe queries (never throw) ----
    bool isNull()   const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray()  const { return type == Type::Array; }
    bool isString() const { return type == Type::String; }
    bool isNumber() const { return type == Type::Number; }
    bool isBool()   const { return type == Type::Bool; }

    // object member by key (nullptr if not an object or the key doesn't exist)
    const Value* member(const std::string& key) const;
    // does the key exist (and is it NOT null)? -- to distinguish "absent/null" from "present"
    bool has(const std::string& key) const;

    // accessors with a default (tolerant of the wrong type)
    std::string asString(const std::string& def = std::string()) const { return type == Type::String ? str : def; }
    double      asNumber(double def = 0.0) const { return type == Type::Number ? num : def; }
    long        asLong(long def = 0) const { return type == Type::Number ? (long)num : def; }
    int         asInt(int def = 0) const { return type == Type::Number ? (int)num : def; }
    bool        asBool(bool def = false) const { return type == Type::Bool ? b : def; }

    // shortcuts: member->value with a default; if missing or null/wrong type -> def
    std::string memberString(const std::string& key, const std::string& def = std::string()) const;
    double      memberNumber(const std::string& key, double def = 0.0) const;
    int         memberInt(const std::string& key, int def = 0) const;
    long        memberLong(const std::string& key, long def = 0) const;
    bool        memberBool(const std::string& key, bool def = false) const;
};

// Parses the whole text. Returns nullptr and fills 'err' on failure.
ValuePtr Parse(const std::string& text, std::string* err = nullptr);

// Reads a file as bytes (binary, no transcoding) and parses it. nullptr if it can't open or parse.
ValuePtr ParseFile(const char* path, std::string* err = nullptr);

} // namespace jsonm
