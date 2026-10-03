#include "Noctis/Core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace noctis
{
namespace jsonimpl
{
const JsonValue kJsonNull;
const std::string kJsonEmptyString;

class Parser
{
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool parseDocument(JsonValue& out)
    {
        skipWs();
        if (!parseValue(out))
        {
            return false;
        }
        skipWs();
        if (pos_ != s_.size())
        {
            return fail("trailing characters after JSON document");
        }
        return true;
    }

    std::string error;

private:
    bool fail(const char* msg)
    {
        if (error.empty())
        {
            int line = 1;
            int col = 1;
            for (size_t i = 0; i < pos_ && i < s_.size(); ++i)
            {
                if (s_[i] == '\n')
                {
                    ++line;
                    col = 1;
                }
                else
                {
                    ++col;
                }
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d:%d: ", line, col);
            error = std::string(buf) + msg;
        }
        return false;
    }

    void skipWs()
    {
        while (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                ++pos_;
            }
            else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/')
            {
                // Tolerate // comments in hand-written data files.
                while (pos_ < s_.size() && s_[pos_] != '\n')
                {
                    ++pos_;
                }
            }
            else
            {
                break;
            }
        }
    }

    bool parseValue(JsonValue& out)
    {
        if (++depth_ > 256)
        {
            return fail("nesting too deep");
        }
        skipWs();
        if (pos_ >= s_.size())
        {
            return fail("unexpected end of input");
        }
        bool ok = false;
        const char c = s_[pos_];
        if (c == '{')
        {
            ok = parseObject(out);
        }
        else if (c == '[')
        {
            ok = parseArray(out);
        }
        else if (c == '"')
        {
            std::string str;
            ok = parseString(str);
            if (ok)
            {
                out = JsonValue(std::move(str));
            }
        }
        else if (c == 't' || c == 'f' || c == 'n')
        {
            ok = parseLiteral(out);
        }
        else
        {
            ok = parseNumber(out);
        }
        --depth_;
        return ok;
    }

    bool parseLiteral(JsonValue& out)
    {
        if (s_.compare(pos_, 4, "true") == 0)
        {
            pos_ += 4;
            out = JsonValue(true);
            return true;
        }
        if (s_.compare(pos_, 5, "false") == 0)
        {
            pos_ += 5;
            out = JsonValue(false);
            return true;
        }
        if (s_.compare(pos_, 4, "null") == 0)
        {
            pos_ += 4;
            out = JsonValue();
            return true;
        }
        return fail("invalid literal");
    }

    bool parseNumber(JsonValue& out)
    {
        const size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+'))
        {
            ++pos_;
        }
        bool digits = false;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9'))
            {
                digits = true;
                ++pos_;
            }
            else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+')
            {
                ++pos_;
            }
            else
            {
                break;
            }
        }
        if (!digits)
        {
            return fail("invalid number");
        }
        const std::string token = s_.substr(start, pos_ - start);
        char* end = nullptr;
        const double v = std::strtod(token.c_str(), &end);
        if (end == token.c_str() || *end != '\0')
        {
            return fail("invalid number");
        }
        out = JsonValue(v);
        return true;
    }

    static void appendUtf8(std::string& out, u32 cp)
    {
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parseHex4(u32& cp)
    {
        if (pos_ + 4 > s_.size())
        {
            return fail("truncated unicode escape");
        }
        cp = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = s_[pos_++];
            cp <<= 4;
            if (c >= '0' && c <= '9')
            {
                cp |= static_cast<u32>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                cp |= static_cast<u32>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                cp |= static_cast<u32>(c - 'A' + 10);
            }
            else
            {
                return fail("invalid unicode escape");
            }
        }
        return true;
    }

    bool parseString(std::string& out)
    {
        ++pos_; // opening quote
        while (pos_ < s_.size())
        {
            const char c = s_[pos_++];
            if (c == '"')
            {
                return true;
            }
            if (c == '\\')
            {
                if (pos_ >= s_.size())
                {
                    break;
                }
                const char e = s_[pos_++];
                switch (e)
                {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    u32 cp = 0;
                    if (!parseHex4(cp))
                    {
                        return false;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u')
                    {
                        pos_ += 2;
                        u32 lo = 0;
                        if (!parseHex4(lo))
                        {
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("invalid escape sequence");
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    bool parseArray(JsonValue& out)
    {
        ++pos_;
        out = JsonValue::makeArray();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ']')
        {
            ++pos_;
            return true;
        }
        while (true)
        {
            JsonValue item;
            if (!parseValue(item))
            {
                return false;
            }
            out.push(std::move(item));
            skipWs();
            if (pos_ >= s_.size())
            {
                return fail("unterminated array");
            }
            if (s_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']')
            {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parseObject(JsonValue& out)
    {
        ++pos_;
        out = JsonValue::makeObject();
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == '}')
        {
            ++pos_;
            return true;
        }
        while (true)
        {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"')
            {
                return fail("expected object key");
            }
            std::string key;
            if (!parseString(key))
            {
                return false;
            }
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':')
            {
                return fail("expected ':'");
            }
            ++pos_;
            JsonValue item;
            if (!parseValue(item))
            {
                return false;
            }
            out.set(key, std::move(item));
            skipWs();
            if (pos_ >= s_.size())
            {
                return fail("unterminated object");
            }
            if (s_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}')
            {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
    int depth_ = 0;
};

void appendNumber(std::string& out, double v)
{
    if (!std::isfinite(v))
    {
        out += "null";
        return;
    }
    char buf[48];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15)
    {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.9g", v);
    }
    out += buf;
}

void appendIndent(std::string& out, int indent)
{
    out.append(static_cast<size_t>(indent) * 2u, ' ');
}
} // namespace jsonimpl

JsonValue JsonValue::makeArray()
{
    JsonValue v;
    v.type_ = Type::Array;
    return v;
}

JsonValue JsonValue::makeObject()
{
    JsonValue v;
    v.type_ = Type::Object;
    return v;
}

const JsonValue& JsonValue::nullValue() { return jsonimpl::kJsonNull; }

const std::string& JsonValue::asString() const
{
    return type_ == Type::String ? string_ : jsonimpl::kJsonEmptyString;
}

size_t JsonValue::size() const
{
    if (type_ == Type::Array)
    {
        return array_.size();
    }
    if (type_ == Type::Object)
    {
        return object_.size();
    }
    return 0;
}

const JsonValue& JsonValue::at(size_t i) const
{
    if (type_ != Type::Array || i >= array_.size())
    {
        return jsonimpl::kJsonNull;
    }
    return array_[i];
}

JsonValue& JsonValue::push(JsonValue v)
{
    if (type_ != Type::Array)
    {
        *this = makeArray();
    }
    array_.push_back(std::move(v));
    return array_.back();
}

bool JsonValue::has(const std::string& key) const
{
    if (type_ != Type::Object)
    {
        return false;
    }
    for (const Member& m : object_)
    {
        if (m.first == key)
        {
            return true;
        }
    }
    return false;
}

const JsonValue& JsonValue::get(const std::string& key) const
{
    if (type_ != Type::Object)
    {
        return jsonimpl::kJsonNull;
    }
    for (const Member& m : object_)
    {
        if (m.first == key)
        {
            return m.second;
        }
    }
    return jsonimpl::kJsonNull;
}

JsonValue& JsonValue::set(const std::string& key, JsonValue v)
{
    if (type_ != Type::Object)
    {
        *this = makeObject();
    }
    for (Member& m : object_)
    {
        if (m.first == key)
        {
            m.second = std::move(v);
            return m.second;
        }
    }
    object_.emplace_back(key, std::move(v));
    return object_.back().second;
}

const JsonValue& JsonValue::path(const std::string& dotted) const
{
    const JsonValue* cur = this;
    size_t start = 0;
    while (start <= dotted.size())
    {
        const size_t dot = dotted.find('.', start);
        const std::string part = dotted.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        cur = &cur->get(part);
        if (cur->isNull() || dot == std::string::npos)
        {
            break;
        }
        start = dot + 1;
    }
    return *cur;
}

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            }
            else
            {
                out.push_back(c);
            }
        }
    }
    return out;
}

void JsonValue::dumpTo(std::string& out, bool pretty, int indent) const
{
    switch (type_)
    {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: jsonimpl::appendNumber(out, number_); break;
    case Type::String:
        out.push_back('"');
        out += jsonEscape(string_);
        out.push_back('"');
        break;
    case Type::Array:
    {
        if (array_.empty())
        {
            out += "[]";
            break;
        }
        // Arrays of scalars stay on one line for readability.
        bool scalars = true;
        for (const JsonValue& v : array_)
        {
            if (v.isArray() || v.isObject())
            {
                scalars = false;
                break;
            }
        }
        out.push_back('[');
        for (size_t i = 0; i < array_.size(); ++i)
        {
            if (pretty && !scalars)
            {
                out.push_back('\n');
                jsonimpl::appendIndent(out, indent + 1);
            }
            array_[i].dumpTo(out, pretty, indent + 1);
            if (i + 1 < array_.size())
            {
                out.push_back(',');
                if (pretty && scalars)
                {
                    out.push_back(' ');
                }
            }
        }
        if (pretty && !scalars)
        {
            out.push_back('\n');
            jsonimpl::appendIndent(out, indent);
        }
        out.push_back(']');
        break;
    }
    case Type::Object:
    {
        if (object_.empty())
        {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (size_t i = 0; i < object_.size(); ++i)
        {
            if (pretty)
            {
                out.push_back('\n');
                jsonimpl::appendIndent(out, indent + 1);
            }
            out.push_back('"');
            out += jsonEscape(object_[i].first);
            out += pretty ? "\": " : "\":";
            object_[i].second.dumpTo(out, pretty, indent + 1);
            if (i + 1 < object_.size())
            {
                out.push_back(',');
            }
        }
        if (pretty)
        {
            out.push_back('\n');
            jsonimpl::appendIndent(out, indent);
        }
        out.push_back('}');
        break;
    }
    }
}

std::string JsonValue::dump(bool pretty) const
{
    std::string out;
    dumpTo(out, pretty, 0);
    return out;
}

JsonParseResult parseJson(const std::string& text)
{
    JsonParseResult result;
    jsonimpl::Parser parser(text);
    result.ok = parser.parseDocument(result.value);
    result.error = parser.error;
    if (!result.ok)
    {
        result.value = JsonValue();
    }
    return result;
}
} // namespace noctis
