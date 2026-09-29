#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pf8::json {
namespace {

const Value& nullValue()
{
    static const Value v;
    return v;
}

class Parser
{
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::optional<Value> run(std::string* error)
    {
        skipWs();
        auto v = value(0);
        skipWs();
        if (v && pos_ != s_.size()) fail("trailing characters");
        if (!v || !error_.empty())
        {
            if (error) *error = error_ + " at offset " + std::to_string(pos_);
            return std::nullopt;
        }
        return v;
    }

private:
    void fail(const char* msg)
    {
        if (error_.empty()) error_ = msg;
    }

    void skipWs()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool consume(char c)
    {
        if (pos_ < s_.size() && s_[pos_] == c)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    bool literal(std::string_view word)
    {
        if (s_.substr(pos_, word.size()) == word)
        {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    std::optional<Value> value(int depth)
    {
        if (depth > 128) { fail("nesting too deep"); return std::nullopt; }
        if (pos_ >= s_.size()) { fail("unexpected end"); return std::nullopt; }
        const char c = s_[pos_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"')
        {
            auto str = string();
            if (!str) return std::nullopt;
            return Value(std::move(*str));
        }
        if (literal("true")) return Value(true);
        if (literal("false")) return Value(false);
        if (literal("null")) return Value();
        if (c == '-' || (c >= '0' && c <= '9')) return number();
        fail("unexpected character");
        return std::nullopt;
    }

    std::optional<Value> number()
    {
        const size_t start = pos_;
        consume('-');
        if (consume('0')) {}
        else if (pos_ < s_.size() && s_[pos_] >= '1' && s_[pos_] <= '9')
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        else { fail("bad number"); return std::nullopt; }
        if (consume('.'))
        {
            if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') { fail("bad fraction"); return std::nullopt; }
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E'))
        {
            ++pos_;
            if (!consume('+')) consume('-');
            if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') { fail("bad exponent"); return std::nullopt; }
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        const std::string text(s_.substr(start, pos_ - start));
        return Value(std::strtod(text.c_str(), nullptr));
    }

    static void appendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::optional<uint32_t> hex4()
    {
        if (pos_ + 4 > s_.size()) return std::nullopt;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return std::nullopt;
        }
        return v;
    }

    std::optional<std::string> string()
    {
        ++pos_; // opening quote
        std::string out;
        while (pos_ < s_.size())
        {
            const char c = s_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) { fail("control character in string"); return std::nullopt; }
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    auto cp = hex4();
                    if (!cp) { fail("bad \\u escape"); return std::nullopt; }
                    if (*cp >= 0xD800 && *cp <= 0xDBFF && s_.substr(pos_, 2) == "\\u")
                    {
                        pos_ += 2;
                        auto lo = hex4();
                        if (!lo || *lo < 0xDC00 || *lo > 0xDFFF) { fail("bad surrogate"); return std::nullopt; }
                        *cp = 0x10000 + ((*cp - 0xD800) << 10) + (*lo - 0xDC00);
                    }
                    appendUtf8(out, *cp);
                    break;
                }
                default: fail("bad escape"); return std::nullopt;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> array(int depth)
    {
        ++pos_;
        Array a;
        skipWs();
        if (consume(']')) return Value(std::move(a));
        for (;;)
        {
            skipWs();
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            a.push_back(std::move(*v));
            skipWs();
            if (consume(']')) return Value(std::move(a));
            if (!consume(',')) { fail("expected , or ]"); return std::nullopt; }
        }
    }

    std::optional<Value> object(int depth)
    {
        ++pos_;
        Object o;
        skipWs();
        if (consume('}')) return Value(std::move(o));
        for (;;)
        {
            skipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') { fail("expected key"); return std::nullopt; }
            auto key = string();
            if (!key) return std::nullopt;
            skipWs();
            if (!consume(':')) { fail("expected :"); return std::nullopt; }
            skipWs();
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            o[std::move(*key)] = std::move(*v);
            skipWs();
            if (consume('}')) return Value(std::move(o));
            if (!consume(',')) { fail("expected , or }"); return std::nullopt; }
        }
    }

    std::string_view s_;
    size_t pos_ = 0;
    std::string error_;
};

void writeString(std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
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
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                    out += static_cast<char>(c);
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double d)
{
    if (!std::isfinite(d)) { out += "null"; return; }
    if (d == std::floor(d) && std::abs(d) < 9.007199254740992e15)
    {
        out += std::to_string(static_cast<int64_t>(d));
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.17g", d);
    out += buf;
}

void write(std::string& out, const Value& v, bool pretty, int indent)
{
    auto newline = [&](int level) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(level) * 2, ' ');
    };
    if (v.isNull()) out += "null";
    else if (v.isBool()) out += v.asBool() ? "true" : "false";
    else if (v.isNumber()) writeNumber(out, v.asNumber());
    else if (v.isString()) writeString(out, v.asString());
    else if (v.isArray())
    {
        const auto& a = v.asArray();
        out += '[';
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (i) out += ',';
            newline(indent + 1);
            write(out, a[i], pretty, indent + 1);
        }
        if (!a.empty()) newline(indent);
        out += ']';
    }
    else
    {
        const auto& o = v.asObject();
        out += '{';
        bool first = true;
        for (const auto& [k, val] : o)
        {
            if (!first) out += ',';
            first = false;
            newline(indent + 1);
            writeString(out, k);
            out += pretty ? ": " : ":";
            write(out, val, pretty, indent + 1);
        }
        if (!o.empty()) newline(indent);
        out += '}';
    }
}

} // namespace

const Array& Value::asArray() const
{
    static const Array empty;
    return isArray() ? *std::get<4>(v_) : empty;
}

const Object& Value::asObject() const
{
    static const Object empty;
    return isObject() ? *std::get<5>(v_) : empty;
}

const Value& Value::operator[](const std::string& key) const
{
    if (!isObject()) return nullValue();
    const auto& o = *std::get<5>(v_);
    auto it = o.find(key);
    return it == o.end() ? nullValue() : it->second;
}

const Value& Value::operator[](size_t index) const
{
    if (!isArray()) return nullValue();
    const auto& a = *std::get<4>(v_);
    return index < a.size() ? a[index] : nullValue();
}

bool Value::has(const std::string& key) const { return isObject() && std::get<5>(v_)->count(key) > 0; }

Array& Value::array()
{
    if (!isArray()) v_ = std::make_shared<Array>();
    return *std::get<4>(v_);
}

Object& Value::object()
{
    if (!isObject()) v_ = std::make_shared<Object>();
    return *std::get<5>(v_);
}

std::optional<Value> parse(std::string_view text, std::string* error) { return Parser(text).run(error); }

std::string serialize(const Value& v, bool pretty)
{
    std::string out;
    write(out, v, pretty, 0);
    if (pretty) out += '\n';
    return out;
}

} // namespace pf8::json
