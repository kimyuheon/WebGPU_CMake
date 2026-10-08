#pragma once

// DXF 읽기 / 쓰기 안쪽에서 함께 쓰는 것들 (밖에는 lot_dxf.h 만 보인다).
// 네이티브 dxf/ 처럼 나눴다: 훑기(lot_dxf_read) -> 펼치기(lot_dxf_emit*) / 쓰기(lot_dxf_write).

#include "lot_dxf.h"
#include "lot_math.h"

#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lot_dxf {
namespace detail {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

inline float toFloat(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
inline int toInt(const std::string& s) { return std::atoi(s.c_str()); }

// ---- 코드/값 쌍 읽기 ----
//
// DXF 는 줄 두 개가 한 쌍이다: 그룹 코드, 값. 코드 앞뒤 공백과 CR 을 털어낸다.
struct Reader {
    const std::string& text;
    size_t pos = 0;

    bool next(int& code, std::string& value) {
        std::string first, second;
        if (!line(first) || !line(second)) return false;
        code = std::atoi(trim(first).c_str());
        value = trim(second);
        return true;
    }

private:
    bool line(std::string& out) {
        if (pos >= text.size()) return false;
        const size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            out.assign(text, pos, text.size() - pos);
            pos = text.size();
        } else {
            out.assign(text, pos, end - pos);
            pos = end + 1;
        }
        return true;
    }
    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
        return s.substr(a, b - a);
    }
};

// 엔티티 하나 분량의 코드/값. 같은 코드가 여러 번 오는 것(폴리선 좌표)도 담는다.
struct Entity {
    std::string type;
    std::multimap<int, std::string> values;
    // 읽은 순서 그대로 (해치 경계처럼 코드 순서가 뜻을 갖는 엔티티만 채운다)
    std::vector<std::pair<int, std::string>> seq;

    std::string str(int code, const std::string& fallback = "") const {
        auto it = values.find(code);
        return it == values.end() ? fallback : it->second;
    }
    float num(int code, float fallback = 0.0f) const {
        auto it = values.find(code);
        return it == values.end() ? fallback : toFloat(it->second);
    }
    int integer(int code, int fallback = 0) const {
        auto it = values.find(code);
        return it == values.end() ? fallback : toInt(it->second);
    }
    bool has(int code) const { return values.find(code) != values.end(); }
    std::vector<float> all(int code) const {
        std::vector<float> out;
        auto range = values.equal_range(code);
        for (auto it = range.first; it != range.second; ++it) out.push_back(toFloat(it->second));
        return out;
    }
};

// HATCH 원문 쌍을 순서대로 읽는다 (네이티브 RawCursor). 앞으로 limit 개 안에서 code 를 찾는다.
struct RawCursor {
    const std::vector<std::pair<int, std::string>>& p;
    size_t i = 0;
    explicit RawCursor(const std::vector<std::pair<int, std::string>>& v) : p(v) {}
    bool find(int code, std::string& v, size_t limit = 400) {
        for (size_t k = i, n = 0; k < p.size() && n < limit; ++k, ++n) {
            if (p[k].first == code) { v = p[k].second; i = k + 1; return true; }
        }
        return false;
    }
    float num(int code, float def = 0.0f) { std::string v; return find(code, v) ? toFloat(v) : def; }
    int integer(int code, int def = 0) { std::string v; return find(code, v) ? toInt(v) : def; }
    bool peek(int code) const { return i < p.size() && p[i].first == code; }
};

// 2D 아핀 변환 (블록 삽입). p' = (a px + b py + tx, c px + d py + ty, pz + tz).
struct Xform {
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
    vec3 t{0.0f, 0.0f, 0.0f};

    vec3 point(const vec3& p) const {
        return vec3{a * p.x + b * p.y + t.x, c * p.x + d * p.y + t.y, p.z + t.z};
    }
    vec3 vector(const vec3& v) const { return vec3{a * v.x + b * v.y, c * v.x + d * v.y, v.z}; }
    // this ∘ o  (o 를 먼저)
    Xform then(const Xform& o) const {
        Xform r;
        r.a = a * o.a + b * o.c;  r.b = a * o.b + b * o.d;
        r.c = c * o.a + d * o.c;  r.d = c * o.b + d * o.d;
        r.t = point(o.t);
        return r;
    }
    float scaleX() const { return std::sqrt(a * a + c * c); }
    float scaleY() const { return std::sqrt(b * b + d * d); }
    float det() const { return a * d - b * c; }
    float rotation() const { return std::atan2(c, a); }
    // 원이 원으로 남는가 (균등 축척 + 회전/이동)
    bool conformal() const {
        const float sx = scaleX(), sy = scaleY();
        return std::fabs(sx - sy) <= 1e-4f * std::fmax(sx, 1e-9f) && std::fabs(a * b + c * d) <= 1e-4f * sx * sy;
    }
};

struct Block {
    vec3 base{0.0f, 0.0f, 0.0f};
    std::vector<Entity> entities;
};

// ---- lot_dxf_util.cpp ----
// AutoCAD Color Index -> RGB
vec3 aciColor(int index);
// 선종류 이름 -> 우리 id (표준 8종). 모르는 이름은 실선.
uint32_t linetypeByName(const std::string& name);
// 폴리선의 bulge (코드 42) - a 와 b 사이의 호 점들 (a, b 는 빼고)
void appendBulgeArc(std::vector<vec3>& out, const vec3& a, const vec3& b, float bulge);
// NURBS 를 점으로 (제어점 · 가중치 · 매듭 · 차수, 없으면 맞춤점 / 제어 다각형)
std::vector<vec3> tessellateSpline(const std::vector<vec3>& P, const std::vector<float>& W,
                                   const std::vector<float>& U, int degree, const std::vector<vec3>& fit);
// TEXT 의 %%c 같은 제어 코드 · \U+XXXX 를 UTF-8 로
std::string decodeTextCodes(const std::string& in);
// MTEXT 서식을 벗겨 줄 목록으로
std::vector<std::string> mtextLines(const std::string& in);
// 헤더 변수 하나의 좌표 ($EXTMIN 처럼 9 이름 다음 10/20). 없으면 false.
bool headerPoint(const std::string& text, const char* name, double& x, double& y);
// 맨 바깥 엔티티의 위치 좌표를 double 로 원점 이동 (멀리 떨어진 도면)
void shiftEntity(Entity& e, double ox, double oy);

}  // namespace detail
}  // namespace lot_dxf
