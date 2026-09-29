#pragma once
// Minimal JSON DOM: parse (RFC 8259, UTF-8) and serialise. Not for the audio thread.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pf8::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

class Value
{
public:
    Value() = default;                       // null
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(int i) : v_(static_cast<double>(i)) {}
    Value(int64_t i) : v_(static_cast<double>(i)) {}
    Value(uint64_t i) : v_(static_cast<double>(i)) {}
    Value(double d) : v_(d) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const noexcept { return v_.index() == 0; }
    bool isBool() const noexcept { return v_.index() == 1; }
    bool isNumber() const noexcept { return v_.index() == 2; }
    bool isString() const noexcept { return v_.index() == 3; }
    bool isArray() const noexcept { return v_.index() == 4; }
    bool isObject() const noexcept { return v_.index() == 5; }

    bool asBool(bool def = false) const noexcept { return isBool() ? std::get<1>(v_) : def; }
    double asNumber(double def = 0.0) const noexcept { return isNumber() ? std::get<2>(v_) : def; }
    int asInt(int def = 0) const noexcept { return isNumber() ? static_cast<int>(std::get<2>(v_)) : def; }
    int64_t asInt64(int64_t def = 0) const noexcept { return isNumber() ? static_cast<int64_t>(std::get<2>(v_)) : def; }
    std::string asString(const std::string& def = {}) const { return isString() ? std::get<3>(v_) : def; }
    const Array& asArray() const;   // empty array when not an array
    const Object& asObject() const; // empty object when not an object

    // Object access: returns null for a missing key / non-object.
    const Value& operator[](const std::string& key) const;
    // Array access: returns null when out of range / non-array.
    const Value& operator[](size_t index) const;
    bool has(const std::string& key) const;

    Array& array();   // converts to an array if needed
    Object& object(); // converts to an object if needed

private:
    std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> v_;
};

std::optional<Value> parse(std::string_view text, std::string* error = nullptr);
std::string serialize(const Value& v, bool pretty = false);

} // namespace pf8::json
