#include "core/Json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace evo {

Json& Json::operator=(const Json& other) {
    if (this == &other) return *this;
    type_ = other.type_;
    b_ = other.b_;
    d_ = other.d_;
    s_ = other.s_ ? std::make_shared<std::string>(*other.s_) : nullptr;
    a_ = other.a_ ? std::make_shared<Array>(*other.a_) : nullptr;
    o_ = other.o_ ? std::make_shared<Object>(*other.o_) : nullptr;
    return *this;
}

static const char* typeName(Json::Type t) {
    switch (t) {
        case Json::Type::Null: return "null";
        case Json::Type::Bool: return "bool";
        case Json::Type::Number: return "number";
        case Json::Type::String: return "string";
        case Json::Type::Array: return "array";
        case Json::Type::Object: return "object";
    }
    return "?";
}

#define EVO_JSON_EXPECT(cond, want) \
    if (!(cond)) throw JsonError(std::string("expected ") + want + ", got " + typeName(type_))

bool Json::asBool() const { EVO_JSON_EXPECT(isBool(), "bool"); return b_; }
double Json::asNumber() const { EVO_JSON_EXPECT(isNumber(), "number"); return d_; }
const std::string& Json::asString() const { EVO_JSON_EXPECT(isString(), "string"); return *s_; }
const Json::Array& Json::asArray() const { EVO_JSON_EXPECT(isArray(), "array"); return *a_; }
Json::Array& Json::asArray() { EVO_JSON_EXPECT(isArray(), "array"); return *a_; }
const Json::Object& Json::asObject() const { EVO_JSON_EXPECT(isObject(), "object"); return *o_; }
Json::Object& Json::asObject() { EVO_JSON_EXPECT(isObject(), "object"); return *o_; }

bool Json::contains(const std::string& key) const {
    return isObject() && o_->count(key) > 0;
}

const Json& Json::at(const std::string& key) const {
    const auto& obj = asObject();
    auto it = obj.find(key);
    if (it == obj.end()) throw JsonError("missing key '" + key + "'");
    return it->second;
}

Json& Json::operator[](const std::string& key) {
    if (isNull()) *this = Json::object();
    return asObject()[key];
}

void Json::push(Json v) {
    if (isNull()) *this = Json::array();
    asArray().push_back(std::move(v));
}

std::size_t Json::size() const {
    if (isArray()) return a_->size();
    if (isObject()) return o_->size();
    return 0;
}

void Json::setPath(const std::string& path, Json value) {
    Json* cur = this;
    std::size_t start = 0;
    while (true) {
        auto dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (dot == std::string::npos) {
            (*cur)[key] = std::move(value);
            return;
        }
        cur = &(*cur)[key];
        start = dot + 1;
    }
}

const Json* Json::findPath(const std::string& path) const {
    const Json* cur = this;
    std::size_t start = 0;
    while (true) {
        auto dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!cur->isObject()) return nullptr;
        auto it = cur->o_->find(key);
        if (it == cur->o_->end()) return nullptr;
        if (dot == std::string::npos) return &it->second;
        cur = &it->second;
        start = dot + 1;
    }
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

std::string formatNumber(double d) {
    if (!std::isfinite(d)) return "null";
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", d);
        return buf;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.10g", d);
    // Ensure a round-trippable value when 10 digits are not enough.
    if (std::strtod(buf, nullptr) != d) std::snprintf(buf, sizeof buf, "%.17g", d);
    return buf;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent * d), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number: out += formatNumber(d_); break;
        case Type::String: out += jsonEscape(*s_); break;
        case Type::Array: {
            out += '[';
            bool first = true;
            // Keep arrays of plain numbers on one line for readability.
            bool flat = true;
            for (const auto& v : *a_) if (v.isArray() || v.isObject()) { flat = false; break; }
            for (const auto& v : *a_) {
                if (!first) out += indent >= 0 && flat ? ", " : ",";
                if (!flat) newline(depth + 1);
                v.dumpTo(out, indent, depth + 1);
                first = false;
            }
            if (!flat && !a_->empty()) newline(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [k, v] : *o_) {
                if (!first) out += ',';
                newline(depth + 1);
                out += jsonEscape(k);
                out += indent >= 0 ? ": " : ":";
                v.dumpTo(out, indent, depth + 1);
                first = false;
            }
            if (!o_->empty()) newline(depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& t) : t_(t) {}

    Json parseDocument() {
        Json v = parseValue();
        skipWs();
        if (i_ != t_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& msg) const {
        std::size_t line = 1, col = 1;
        for (std::size_t k = 0; k < i_ && k < t_.size(); ++k) {
            if (t_[k] == '\n') { ++line; col = 1; } else { ++col; }
        }
        throw JsonError("JSON parse error at line " + std::to_string(line) + ", column " +
                        std::to_string(col) + ": " + msg);
    }

    void skipWs() {
        while (i_ < t_.size()) {
            char c = t_[i_];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { ++i_; continue; }
            // Allow // line comments in config files.
            if (c == '/' && i_ + 1 < t_.size() && t_[i_ + 1] == '/') {
                while (i_ < t_.size() && t_[i_] != '\n') ++i_;
                continue;
            }
            break;
        }
    }

    Json parseValue() {
        skipWs();
        if (i_ >= t_.size()) fail("unexpected end of input");
        char c = t_[i_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Json(parseString());
        if (c == 't' && t_.compare(i_, 4, "true") == 0) { i_ += 4; return Json(true); }
        if (c == 'f' && t_.compare(i_, 5, "false") == 0) { i_ += 5; return Json(false); }
        if (c == 'n' && t_.compare(i_, 4, "null") == 0) { i_ += 4; return Json(); }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail(std::string("unexpected character '") + c + "'");
    }

    Json parseNumber() {
        const char* begin = t_.c_str() + i_;
        char* end = nullptr;
        double d = std::strtod(begin, &end);
        if (end == begin) fail("invalid number");
        i_ += static_cast<std::size_t>(end - begin);
        return Json(d);
    }

    std::string parseString() {
        ++i_;  // opening quote
        std::string out;
        while (true) {
            if (i_ >= t_.size()) fail("unterminated string");
            char c = t_[i_++];
            if (c == '"') break;
            if (c != '\\') { out += c; continue; }
            if (i_ >= t_.size()) fail("bad escape");
            char e = t_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (i_ + 4 > t_.size()) fail("bad unicode escape");
                    unsigned cp = static_cast<unsigned>(std::strtoul(t_.substr(i_, 4).c_str(), nullptr, 16));
                    i_ += 4;
                    if (cp < 0x80) out += static_cast<char>(cp);
                    else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: fail("bad escape");
            }
        }
        return out;
    }

    Json parseArray() {
        ++i_;
        Json arr = Json::array();
        skipWs();
        if (i_ < t_.size() && t_[i_] == ']') { ++i_; return arr; }
        while (true) {
            arr.push(parseValue());
            skipWs();
            if (i_ >= t_.size()) fail("unterminated array");
            if (t_[i_] == ',') { ++i_; continue; }
            if (t_[i_] == ']') { ++i_; return arr; }
            fail("expected ',' or ']'");
        }
    }

    Json parseObject() {
        ++i_;
        Json obj = Json::object();
        skipWs();
        if (i_ < t_.size() && t_[i_] == '}') { ++i_; return obj; }
        while (true) {
            skipWs();
            if (i_ >= t_.size() || t_[i_] != '"') fail("expected string key");
            std::string key = parseString();
            skipWs();
            if (i_ >= t_.size() || t_[i_] != ':') fail("expected ':'");
            ++i_;
            obj.asObject()[key] = parseValue();
            skipWs();
            if (i_ >= t_.size()) fail("unterminated object");
            if (t_[i_] == ',') { ++i_; continue; }
            if (t_[i_] == '}') { ++i_; return obj; }
            fail("expected ',' or '}'");
        }
    }

    const std::string& t_;
    std::size_t i_ = 0;
};

}  // namespace

Json Json::parse(const std::string& text) { return Parser(text).parseDocument(); }

}  // namespace evo
