#include "lot_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------- JsonValue

const JsonValue* JsonValue::find(const char* key) const {
    if (!isObject()) return nullptr;
    for (const auto& kv : object) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

JsonValue& JsonValue::set(const std::string& key, JsonValue value) {
    if (!isObject()) {
        *this = makeObject();
    }
    for (auto& kv : object) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return kv.second;
        }
    }
    object.emplace_back(key, std::move(value));
    return object.back().second;
}

JsonValue& JsonValue::push(JsonValue value) {
    if (!isArray()) {
        *this = makeArray();
    }
    array.push_back(std::move(value));
    return array.back();
}

std::vector<float> JsonValue::numbers() const {
    std::vector<float> out;
    if (!isArray()) return out;
    out.reserve(array.size());
    for (const JsonValue& v : array) out.push_back(static_cast<float>(v.numberOr(0.0)));
    return out;
}

// ---------------------------------------------------------------- parser

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool parse(JsonValue& out, std::string& error) {
        skipWs();
        if (!value(out)) {
            error = error_ + " at offset " + std::to_string(pos_);
            return false;
        }
        skipWs();
        if (pos_ != s_.size()) {
            error = "trailing characters at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    bool fail(const char* msg) {
        if (error_.empty()) error_ = msg;
        return false;
    }

    void skipWs() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool literal(const char* word, JsonValue& out, JsonValue value) {
        const size_t n = std::strlen(word);
        if (s_.compare(pos_, n, word) != 0) return fail("unexpected token");
        pos_ += n;
        out = std::move(value);
        return true;
    }

    bool value(JsonValue& out) {
        if (pos_ >= s_.size()) return fail("unexpected end of input");
        const char c = s_[pos_];
        switch (c) {
        case '{': return objectValue(out);
        case '[': return arrayValue(out);
        case '"': {
            std::string str;
            if (!stringValue(str)) return false;
            out = JsonValue(std::move(str));
            return true;
        }
        case 't': return literal("true", out, JsonValue(true));
        case 'f': return literal("false", out, JsonValue(false));
        case 'n': return literal("null", out, JsonValue());
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return numberValue(out);
            return fail("unexpected character");
        }
    }

    bool numberValue(JsonValue& out) {
        const char* begin = s_.c_str() + pos_;
        char* end = nullptr;
        const double d = std::strtod(begin, &end);
        if (end == begin) return fail("bad number");
        pos_ += static_cast<size_t>(end - begin);
        out = JsonValue(d);
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned& out) {
        if (pos_ + 4 > s_.size()) return fail("bad \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool stringValue(std::string& out) {
        if (!consume('"')) return fail("expected string");
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
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
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                // 서로게이트 쌍
                if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 6 <= s_.size()
                    && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                    pos_ += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool arrayValue(JsonValue& out) {
        consume('[');
        out = JsonValue::makeArray();
        skipWs();
        if (consume(']')) return true;
        for (;;) {
            JsonValue item;
            skipWs();
            if (!value(item)) return false;
            out.array.push_back(std::move(item));
            skipWs();
            if (consume(',')) continue;
            if (consume(']')) return true;
            return fail("expected ',' or ']'");
        }
    }

    bool objectValue(JsonValue& out) {
        consume('{');
        out = JsonValue::makeObject();
        skipWs();
        if (consume('}')) return true;
        for (;;) {
            skipWs();
            std::string key;
            if (!stringValue(key)) return false;
            skipWs();
            if (!consume(':')) return fail("expected ':'");
            skipWs();
            JsonValue item;
            if (!value(item)) return false;
            out.object.emplace_back(std::move(key), std::move(item));
            skipWs();
            if (consume(',')) continue;
            if (consume('}')) return true;
            return fail("expected ',' or '}'");
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
    std::string error_;
};

// ---------------------------------------------------------------- writer

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double d) {
    if (!std::isfinite(d)) {
        out += "0";  // JSON 에는 NaN/Inf 가 없다
        return;
    }
    // 정수는 정수처럼 (인덱스 배열이 "3.0" 으로 가득 차지 않게), 나머지는 float 정밀도로.
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", d);
        out += buf;
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", d);
    out += buf;
}

void newline(std::string& out, int indent, int depth) {
    if (indent <= 0) return;
    out += '\n';
    out.append(static_cast<size_t>(indent * depth), ' ');
}

// 숫자만 든 짧은 배열은 한 줄에 - 좌표 [x, y, z] 가 세 줄로 퍼지지 않게
bool isFlatNumberArray(const JsonValue& v) {
    if (!v.isArray() || v.array.size() > 16) return false;
    for (const JsonValue& e : v.array) {
        if (!e.isNumber()) return false;
    }
    return true;
}

void write(std::string& out, const JsonValue& v, int indent, int depth) {
    switch (v.type) {
    case JsonValue::Type::Null: out += "null"; break;
    case JsonValue::Type::Bool: out += v.boolean ? "true" : "false"; break;
    case JsonValue::Type::Number: writeNumber(out, v.number); break;
    case JsonValue::Type::String: writeString(out, v.string); break;
    case JsonValue::Type::Array: {
        if (v.array.empty()) {
            out += "[]";
            break;
        }
        const bool flat = isFlatNumberArray(v);
        out += '[';
        for (size_t i = 0; i < v.array.size(); ++i) {
            if (i > 0) out += ',';
            if (flat) {
                if (i > 0 && indent > 0) out += ' ';
            } else {
                newline(out, indent, depth + 1);
            }
            write(out, v.array[i], indent, depth + 1);
        }
        if (!flat) newline(out, indent, depth);
        out += ']';
        break;
    }
    case JsonValue::Type::Object: {
        if (v.object.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (size_t i = 0; i < v.object.size(); ++i) {
            if (i > 0) out += ',';
            newline(out, indent, depth + 1);
            writeString(out, v.object[i].first);
            out += indent > 0 ? ": " : ":";
            write(out, v.object[i].second, indent, depth + 1);
        }
        newline(out, indent, depth);
        out += '}';
        break;
    }
    }
}

}  // namespace

bool parseJson(const std::string& text, JsonValue& out, std::string& error) {
    Parser p(text);
    return p.parse(out, error);
}

std::string dumpJson(const JsonValue& value, int indent) {
    std::string out;
    write(out, value, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}
