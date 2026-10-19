#pragma once
// Minimal JSON value, parser and writer. Self-contained so the engine has no
// third-party runtime dependency (important for the Emscripten build).
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo {

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() = default;
    Json(const Json& other) { *this = other; }
    Json& operator=(const Json& other);
    Json(Json&&) noexcept = default;
    Json& operator=(Json&&) noexcept = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(double d) : type_(Type::Number), d_(d) {}
    Json(int i) : type_(Type::Number), d_(i) {}
    Json(std::int64_t i) : type_(Type::Number), d_(static_cast<double>(i)) {}
    Json(std::uint64_t i) : type_(Type::Number), d_(static_cast<double>(i)) {}
    Json(const char* s) : type_(Type::String), s_(std::make_shared<std::string>(s)) {}
    Json(std::string s) : type_(Type::String), s_(std::make_shared<std::string>(std::move(s))) {}
    Json(Array a) : type_(Type::Array), a_(std::make_shared<Array>(std::move(a))) {}
    Json(Object o) : type_(Type::Object), o_(std::make_shared<Object>(std::move(o))) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const;
    double asNumber() const;
    const std::string& asString() const;
    const Array& asArray() const;
    Array& asArray();
    const Object& asObject() const;
    Object& asObject();

    bool contains(const std::string& key) const;
    const Json& at(const std::string& key) const;
    Json& operator[](const std::string& key);  // creates object/key on demand
    void push(Json v);
    std::size_t size() const;

    std::string dump(int indent = -1) const;
    static Json parse(const std::string& text);

    // Dotted-path access used for experiment overrides: "mutation.fixedRate".
    void setPath(const std::string& dottedPath, Json value);
    const Json* findPath(const std::string& dottedPath) const;

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool b_ = false;
    double d_ = 0.0;
    std::shared_ptr<std::string> s_;
    std::shared_ptr<Array> a_;
    std::shared_ptr<Object> o_;
};

std::string jsonEscape(const std::string& s);
std::string formatNumber(double d);

}  // namespace evo
