#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// 작은 JSON 값 / 파서 / 직렬화.
//
// .lot 씬 파일 하나 읽고 쓰는 데 nlohmann/json 을 끌어오면 wasm 이 수백 KB
// 커진다. 필요한 건 트리 만들기, 키로 찾기, 숫자 배열 읽기뿐이라 직접 짰다.
// 스펙은 RFC 8259 를 따르되 실용 범위로: 문자열 이스케이프(\uXXXX 포함),
// 숫자는 double, 주석/후행 쉼표는 허용하지 않는다.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;  // 삽입 순서 유지 (저장 파일이 읽기 좋게)

    JsonValue() = default;
    JsonValue(bool b) : type(Type::Bool), boolean(b) {}
    JsonValue(int n) : type(Type::Number), number(n) {}
    JsonValue(unsigned n) : type(Type::Number), number(n) {}
    JsonValue(float n) : type(Type::Number), number(n) {}
    JsonValue(double n) : type(Type::Number), number(n) {}
    JsonValue(const char* s) : type(Type::String), string(s) {}
    JsonValue(std::string s) : type(Type::String), string(std::move(s)) {}

    static JsonValue makeArray() { JsonValue v; v.type = Type::Array; return v; }
    static JsonValue makeObject() { JsonValue v; v.type = Type::Object; return v; }

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    // 객체 멤버. 없으면 nullptr.
    const JsonValue* find(const char* key) const;
    // 객체 멤버 추가/교체 (객체가 아니면 객체로 바꾼다).
    JsonValue& set(const std::string& key, JsonValue value);
    // 배열 뒤에 추가 (배열이 아니면 배열로 바꾼다).
    JsonValue& push(JsonValue value);

    // 편의 읽기 - 타입이 다르면 기본값.
    double numberOr(double fallback) const { return isNumber() ? number : fallback; }
    bool boolOr(bool fallback) const { return isBool() ? boolean : fallback; }
    const std::string& stringOr(const std::string& fallback) const {
        return isString() ? string : fallback;
    }
    size_t size() const { return isArray() ? array.size() : (isObject() ? object.size() : 0); }

    // 숫자 배열을 통째로. 숫자가 아닌 원소는 0.
    std::vector<float> numbers() const;
};

// 파싱. 실패하면 false 와 위치가 담긴 오류 메시지.
bool parseJson(const std::string& text, JsonValue& out, std::string& error);

// 직렬화. indent > 0 이면 줄바꿈/들여쓰기, 0 이면 한 줄.
std::string dumpJson(const JsonValue& value, int indent = 2);
