// Minimal JSON DOM (no exceptions). Used for scientific data files, saves and reports.
#pragma once

#include "Noctis/Core/Platform.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace noctis
{
class NOCTIS_API JsonValue
{
public:
    enum class Type : u8
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    using Member = std::pair<std::string, JsonValue>;

    JsonValue() = default;
    JsonValue(std::nullptr_t) {}
    JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    JsonValue(double n) : type_(Type::Number), number_(n) {}
    JsonValue(float n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    JsonValue(int n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    JsonValue(u32 n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    JsonValue(i64 n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    JsonValue(u64 n) : type_(Type::Number), number_(static_cast<double>(n)) {}
    JsonValue(const char* s) : type_(Type::String), string_(s ? s : "") {}
    JsonValue(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static JsonValue makeArray();
    static JsonValue makeObject();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Lenient accessors: return the fallback on type mismatch.
    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double asDouble(double fallback = 0.0) const { return type_ == Type::Number ? number_ : fallback; }
    float asFloat(float fallback = 0.0f) const { return type_ == Type::Number ? static_cast<float>(number_) : fallback; }
    int asInt(int fallback = 0) const { return type_ == Type::Number ? static_cast<int>(number_) : fallback; }
    const std::string& asString() const;
    std::string asString(const std::string& fallback) const { return type_ == Type::String ? string_ : fallback; }

    // Arrays
    size_t size() const;
    const JsonValue& at(size_t i) const;
    JsonValue& push(JsonValue v);
    const std::vector<JsonValue>& items() const { return array_; }

    // Objects (insertion order preserved)
    bool has(const std::string& key) const;
    const JsonValue& get(const std::string& key) const; // returns a shared null value when absent
    JsonValue& set(const std::string& key, JsonValue v);
    const std::vector<Member>& members() const { return object_; }
    // Dotted path lookup ("locomotion.max_speed_ms").
    const JsonValue& path(const std::string& dotted) const;

    float getFloat(const std::string& key, float fallback) const { return get(key).asFloat(fallback); }
    int getInt(const std::string& key, int fallback) const { return get(key).asInt(fallback); }
    bool getBool(const std::string& key, bool fallback) const { return get(key).asBool(fallback); }
    std::string getString(const std::string& key, const std::string& fallback = std::string()) const
    {
        return get(key).asString(fallback);
    }

    std::string dump(bool pretty = true) const;

    static const JsonValue& nullValue();

private:
    void dumpTo(std::string& out, bool pretty, int indent) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<Member> object_;
};

struct JsonParseResult
{
    JsonValue value;
    bool ok = false;
    std::string error; // "line:col: message"
};

NOCTIS_API JsonParseResult parseJson(const std::string& text);
NOCTIS_API std::string jsonEscape(const std::string& s);
} // namespace noctis
