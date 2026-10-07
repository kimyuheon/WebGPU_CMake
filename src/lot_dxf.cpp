#include "lot_dxf.h"
#include "lot_dimension.h"
#include "lot_linetype.h"
#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace lot_dxf {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

// ---- AutoCAD Color Index -> RGB ----
//
// 1..9 는 잘 알려진 고정 색, 10..249 는 24 색상 × (채도 2 × 명도 5), 250..255 는 회색.
// 정확한 표를 통째로 박는 대신 그 구조를 그대로 계산한다 - 몇 단계 밝기가 원본과
// 미세하게 다를 수 있지만 도면을 보는 데는 문제가 없다.
vec3 hsvToRgb(float h, float s, float v) {
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return vec3{r + m, g + m, b + m};
}

vec3 aciColor(int index) {
    static const vec3 kBasic[10] = {
        {1.0f, 1.0f, 1.0f},  // 0 ByBlock - 흰색으로 본다
        {1.0f, 0.0f, 0.0f},  // 1 red
        {1.0f, 1.0f, 0.0f},  // 2 yellow
        {0.0f, 1.0f, 0.0f},  // 3 green
        {0.0f, 1.0f, 1.0f},  // 4 cyan
        {0.0f, 0.0f, 1.0f},  // 5 blue
        {1.0f, 0.0f, 1.0f},  // 6 magenta
        {0.9f, 0.9f, 0.9f},  // 7 white/black - 어두운 배경이라 밝게
        {0.5f, 0.5f, 0.5f},  // 8 dark grey
        {0.75f, 0.75f, 0.75f},  // 9 light grey
    };
    if (index >= 0 && index < 10) return kBasic[index];
    if (index >= 250 && index <= 255) {
        const float g = (51 + (index - 250) * 41) / 255.0f;
        return vec3{g, g, g};
    }
    if (index >= 10 && index <= 249) {
        const int i = index - 10;
        const float hue = static_cast<float>(i / 10) * 15.0f;  // 24 색상
        const int sub = i % 10;
        const float sat = (sub % 2 == 0) ? 1.0f : 0.5f;
        const float val = 1.0f - 0.18f * static_cast<float>(sub / 2);
        return hsvToRgb(hue, sat, val);
    }
    return vec3{0.9f, 0.9f, 0.9f};
}

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

float toFloat(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
int toInt(const std::string& s) { return std::atoi(s.c_str()); }

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

// 선종류 이름 -> 우리 id (표준 8종). 모르는 이름은 실선.
uint32_t linetypeByName(const std::string& name) {
    for (const lot_linetype::Definition& d : lot_linetype::standard()) {
        if (name == d.name) return d.id;
    }
    // DXF 는 대문자로 적는 일이 많다 (DASHED, HIDDEN, CENTER…)
    std::string upper;
    for (char c : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const lot_linetype::Definition& d : lot_linetype::standard()) {
        std::string dn;
        for (const char* p = d.name; *p; ++p) dn += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        if (upper == dn) return d.id;
    }
    return lot_linetype::kContinuous;
}

// 폴리선의 bulge (코드 42) - 두 점 사이가 직선이 아니라 호다.
// bulge = tan(사잇각 / 4). 부호는 방향 (양수 = 반시계).
void appendBulgeArc(std::vector<vec3>& out, const vec3& a, const vec3& b, float bulge) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float chord = std::sqrt(dx * dx + dy * dy);
    if (chord < 1e-9f || std::fabs(bulge) < 1e-9f) return;

    const float theta = 4.0f * std::atan(bulge);      // 사잇각
    const float radius = chord / (2.0f * std::sin(std::fabs(theta) / 2.0f));
    // 현의 중점에서 수직으로 sagitta 만큼 떨어진 곳이 중심
    const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    const float h = std::sqrt(std::fmax(radius * radius - chord * chord * 0.25f, 0.0f));
    const float sign = (bulge > 0.0f) ? 1.0f : -1.0f;
    const float dir = (std::fabs(theta) > kPi) ? -1.0f : 1.0f;  // 반원보다 크면 중심이 반대쪽
    const float cx = mx - dy / chord * h * sign * dir;
    const float cy = my + dx / chord * h * sign * dir;

    const float a0 = std::atan2(a.y - cy, a.x - cx);
    int segments = static_cast<int>(std::fabs(theta) / (kPi / 12.0f)) + 2;  // 15도쯤마다
    for (int i = 1; i < segments; ++i) {
        const float t = a0 + theta * static_cast<float>(i) / static_cast<float>(segments);
        out.push_back(vec3{cx + radius * std::cos(t), cy + radius * std::sin(t), a.z});
    }
}

// NURBS 한 점 (de Boor) - 네이티브 lot_dxf_flatten evalNurbs 와 같다. U.size() == P.size()+p+1.
vec3 evalNurbs(const std::vector<vec3>& P, const std::vector<float>& W, const std::vector<float>& U, int p, float u) {
    const int n = static_cast<int>(P.size()) - 1;
    int k = p;
    if (u >= U[static_cast<size_t>(n + 1)]) k = n;
    else { while (k < n && !(u < U[static_cast<size_t>(k + 1)])) ++k; }
    std::vector<vec3> d(static_cast<size_t>(p + 1));
    std::vector<float> dw(static_cast<size_t>(p + 1));
    for (int j = 0; j <= p; ++j) {
        const int i = k - p + j;
        const float w = (i < static_cast<int>(W.size())) ? W[static_cast<size_t>(i)] : 1.0f;
        d[static_cast<size_t>(j)] = P[static_cast<size_t>(i)] * w;
        dw[static_cast<size_t>(j)] = w;
    }
    for (int r = 1; r <= p; ++r) {
        for (int j = p; j >= r; --j) {
            const int i = k - p + j;
            const float den = U[static_cast<size_t>(i + p - r + 1)] - U[static_cast<size_t>(i)];
            const float alpha = den > 0.0f ? (u - U[static_cast<size_t>(i)]) / den : 0.0f;
            d[static_cast<size_t>(j)] = d[static_cast<size_t>(j - 1)] * (1.0f - alpha) + d[static_cast<size_t>(j)] * alpha;
            dw[static_cast<size_t>(j)] = dw[static_cast<size_t>(j - 1)] * (1.0f - alpha) + dw[static_cast<size_t>(j)] * alpha;
        }
    }
    const float w = dw[static_cast<size_t>(p)];
    return w != 0.0f ? d[static_cast<size_t>(p)] * (1.0f / w) : d[static_cast<size_t>(p)];
}

// 스플라인 -> 점열. 매듭이 맞으면 NURBS 를 구간마다 12 점, 아니면 맞춤점, 그도 없으면 제어 다각형.
std::vector<vec3> tessellateSpline(const std::vector<vec3>& P, const std::vector<float>& W,
                                   const std::vector<float>& U, int degree, const std::vector<vec3>& fit) {
    const int p = std::max(1, degree);
    if (P.size() >= static_cast<size_t>(p + 1) && U.size() == P.size() + static_cast<size_t>(p) + 1) {
        const int n = static_cast<int>(P.size()) - 1;
        std::vector<vec3> out;
        for (int i = p; i <= n; ++i) {
            const float u0 = U[static_cast<size_t>(i)], u1 = U[static_cast<size_t>(i + 1)];
            if (u1 <= u0) continue;
            for (int s = 0; s < 12; ++s) out.push_back(evalNurbs(P, W, U, p, u0 + (u1 - u0) * static_cast<float>(s) / 12.0f));
        }
        out.push_back(evalNurbs(P, W, U, p, U[static_cast<size_t>(n + 1)]));
        return out;
    }
    if (fit.size() >= 2) return fit;
    return P;
}

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

// 코드 포인트 하나를 UTF-8 로
void appendUtf8(std::string& out, unsigned cp) {
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

// TEXT 의 %% 제어 코드: %%c 지름 · %%d 도 · %%p 플마 · %%u/%%o 밑줄/윗줄(버린다) · %%nnn 문자 코드
std::string decodeTextCodes(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() && in[i + 1] == '%') {
            const char k = static_cast<char>(std::tolower(static_cast<unsigned char>(in[i + 2])));
            if (k == 'c') { appendUtf8(out, 0x2300); i += 2; continue; }   // ⌀
            if (k == 'd') { appendUtf8(out, 0x00B0); i += 2; continue; }   // °
            if (k == 'p') { appendUtf8(out, 0x00B1); i += 2; continue; }   // ±
            if (k == 'u' || k == 'o' || k == 'k') { i += 2; continue; }
            if (k == '%') { out += '%'; i += 2; continue; }
            if (std::isdigit(static_cast<unsigned char>(in[i + 2]))) {
                size_t j = i + 2;
                unsigned v = 0;
                while (j < in.size() && j < i + 5 && std::isdigit(static_cast<unsigned char>(in[j]))) v = v * 10 + (in[j++] - '0');
                appendUtf8(out, v);
                i = j - 1;
                continue;
            }
        }
        out += in[i];
    }
    return out;
}

// MTEXT 서식을 벗겨 줄 단위로 나눈다. \P 줄바꿈, {..} 묶음, \f..; \H..; \C..; 같은 서식은 버리고,
// \S위^아래; 쌓기는 위/아래 로, \U+XXXX 는 그 글자로.
std::vector<std::string> mtextLines(const std::string& in) {
    std::vector<std::string> lines(1);
    for (size_t i = 0; i < in.size(); ++i) {
        const char ch = in[i];
        if (ch == '{' || ch == '}') continue;
        if (ch != '\\' || i + 1 >= in.size()) { lines.back() += ch; continue; }
        const char k = in[++i];
        switch (k) {
        case 'P': case 'X': lines.emplace_back(); break;
        case '~': lines.back() += ' '; break;
        case '\\': case '{': case '}': lines.back() += k; break;
        case 'L': case 'l': case 'O': case 'o': case 'K': case 'k': break;   // 밑줄/윗줄/취소선 켜고 끄기
        case 'U': case 'u':
            if (i + 5 < in.size() + 1 && in[i + 1] == '+') {
                const unsigned cp = static_cast<unsigned>(std::strtoul(in.substr(i + 2, 4).c_str(), nullptr, 16));
                appendUtf8(lines.back(), cp);
                i += 5;
            }
            break;
        case 'S': {   // 쌓기: \S위^아래;  \S위/아래;  \S위#아래;
            const size_t semi = in.find(';', i + 1);
            std::string body = in.substr(i + 1, (semi == std::string::npos ? in.size() : semi) - i - 1);
            for (char& c : body) if (c == '^' || c == '#') c = '/';
            lines.back() += body;
            i = (semi == std::string::npos) ? in.size() : semi;
            break;
        }
        default: {
            // \f글꼴; \H높이; \C색; \c; \T; \Q; \W; \A; \p; 처럼 ; 까지가 서식
            const size_t semi = in.find(';', i + 1);
            if (semi == std::string::npos) { i = in.size(); break; }
            i = semi;
            break;
        }
        }
    }
    for (std::string& l : lines) l = decodeTextCodes(l);
    return lines;
}

// 헤더 변수 하나의 좌표 ($EXTMIN 처럼 9 이름 다음 10/20). 없으면 false.
bool headerPoint(const std::string& text, const char* name, double& x, double& y) {
    const size_t at = text.find(name);
    if (at == std::string::npos || at > 200000) return false;   // 헤더는 앞쪽에 있다
    // 이름 줄 다음부터 코드/값 쌍이다
    Reader r{text};
    r.pos = text.find('\n', at) + 1;
    int code = 0;
    std::string value;
    bool gotX = false, gotY = false;
    for (int i = 0; i < 4 && r.next(code, value); ++i) {
        if (code == 10) { x = std::strtod(value.c_str(), nullptr); gotX = true; }
        else if (code == 20) { y = std::strtod(value.c_str(), nullptr); gotY = true; }
        else if (code == 9 || code == 0) break;
    }
    // 빈 도면은 1e20 같은 표지값을 넣는다
    return gotX && gotY && std::fabs(x) < 1e19 && std::fabs(y) < 1e19;
}

// 맨 바깥 엔티티의 위치 좌표를 double 로 옮겨 적는다. float 로 읽은 뒤 빼면 이미 늦다 -
// 5천만 근처의 float 간격은 4 다. OCS 가 -Z (거울) 인 엔티티는 x 가 뒤집혀 있으므로 반대로.
void shiftEntity(Entity& e, double ox, double oy) {
    if (ox == 0.0 && oy == 0.0) return;
    const std::string& t = e.type;
    std::vector<int> codes;
    if (t == "MTEXT") codes = {10};                 // 11 은 방향 벡터
    else if (t == "INSERT") codes = {10};
    else if (t == "ELLIPSE" || t == "ACAD_TABLE") codes = {10};   // 11 은 장축 / 가로 방향 벡터
    else if (t == "LEADER") codes = {10};
    else if (t == "DIMENSION" || t == "HATCH") return;   // 블록 / 경계 쪽에서 통째로 옮긴다
    else codes = {10, 11, 12, 13};
    const bool flipped = e.num(230, 1.0f) < 0.0f && t != "LINE" && t != "MTEXT" && t != "SPLINE"
                         && t != "ELLIPSE" && t != "LEADER" && t != "3DFACE" && t != "MESH" && t != "PMESH";
    const double sx = flipped ? -ox : ox;
    auto fmt = [](double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        return std::string(buf);
    };
    for (auto& kv : e.values) {
        for (int c : codes) {
            if (kv.first == c) kv.second = fmt(std::strtod(kv.second.c_str(), nullptr) - sx);
            else if (kv.first == c + 10) kv.second = fmt(std::strtod(kv.second.c_str(), nullptr) - oy);
        }
    }
}

struct Block {
    vec3 base{0.0f, 0.0f, 0.0f};
    std::vector<Entity> entities;
};

}  // namespace

LoadStats load(const std::string& text, LotGameObject::Map& objects, LotLayers& layers) {
    LoadStats stats;
    objects.clear();
    layers.clear();

    std::unordered_map<std::string, uint32_t> layerIds;
    layerIds.emplace("0", LotLayers::kDefault);

    // 측량 좌표처럼 원점에서 아주 먼 도면은 float 정밀도가 모자라 (5천만이면 4 단위 간격)
    // 확대하면 선이 떨린다. 도면 범위의 중심을 원점 근처로 옮겨 읽고, 옮긴 양은
    // stats.originX/Y 에 남긴다 (DXF 로 다시 쓸 때 더해 돌려놓는다).
    {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (headerPoint(text, "$EXTMIN", x0, y0) && headerPoint(text, "$EXTMAX", x1, y1)
            && x1 >= x0 && y1 >= y0) {
            const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
            if (std::fabs(cx) > 1e5 || std::fabs(cy) > 1e5) {
                stats.originX = std::round(cx / 1000.0) * 1000.0;
                stats.originY = std::round(cy / 1000.0) * 1000.0;
            }
        }
    }

    auto noteSkipped = [&](const std::string& type) {
        ++stats.skipped;
        if (stats.skippedKinds.find(type) == std::string::npos) {
            if (!stats.skippedKinds.empty()) stats.skippedKinds += ", ";
            stats.skippedKinds += type;
        }
    };

    auto layerOf = [&](const Entity& e) {
        const std::string name = e.str(8, "0");
        auto it = layerIds.find(name);
        if (it != layerIds.end()) return it->second;
        const uint32_t id = layers.create(name, vec3{0.8f, 0.8f, 0.85f});
        layerIds.emplace(name, id);
        return id;
    };

    // 엔티티 색: 62 가 있으면 그 색, 없거나 256 이면 층 따름.
    auto colorOf = [&](const Entity& e, uint32_t layerId, bool& byLayer) {
        const int aci = e.integer(62, 256);
        if (aci == 256 || aci <= 0) {
            byLayer = true;
            const LotLayers::Layer* l = layers.find(layerId);
            return l ? l->color : vec3{0.9f, 0.9f, 0.9f};
        }
        byLayer = false;
        return aciColor(aci);
    };

    auto addSketch = [&](std::vector<vec3> pts, bool closed, const Entity& e,
                         const LotGameObject::Curve* curve) {
        if (pts.size() < 2) return;
        const uint32_t layerId = layerOf(e);
        bool byLayer = false;
        const vec3 color = colorOf(e, layerId, byLayer);

        vec3 origin{0.0f, 0.0f, 0.0f};
        if (curve) {
            origin = curve->center;
        } else {
            for (const vec3& p : pts) origin = origin + p;
            origin = origin * (1.0f / static_cast<float>(pts.size()));
        }
        auto obj = LotGameObject::createGameObject();
        obj.transform.translation = origin;
        obj.color = color;
        obj.colorByLayer = byLayer;
        obj.layer = layerId;
        obj.linetype = e.has(6) ? linetypeByName(e.str(6)) : lot_linetype::kByLayer;
        obj.closed = closed;
        obj.points.reserve(pts.size());
        for (const vec3& p : pts) obj.points.push_back(p - origin);
        if (curve) {
            obj.curve = *curve;
            obj.curve.center = vec3{0.0f, 0.0f, 0.0f};
        }
        const auto id = obj.getId();
        objects.emplace(id, std::move(obj));
    };

    // 글자 한 줄. at 은 월드 기준점, right 는 진행 방향 (단위), height 는 월드 높이.
    auto addText = [&](const std::string& content, const Entity& e, const vec3& at,
                       const vec3& right, float height, int hAlign, int vAlign) {
        if (content.empty() || !(height > 0.0f)) return;
        const uint32_t layerId = layerOf(e);
        bool byLayer = false;
        const vec3 color = colorOf(e, layerId, byLayer);
        auto obj = LotGameObject::createGameObject();
        obj.transform.translation = at;
        obj.color = color;
        obj.colorByLayer = byLayer;
        obj.layer = layerId;
        obj.text.valid = true;
        obj.text.content = content;
        obj.text.height = height;
        obj.text.right = right;
        // 거울 삽입이어도 글자는 읽히게 (AutoCAD 의 MIRRTEXT 0) - 위는 늘 진행 방향의 왼쪽
        obj.text.up = vec3{-right.y, right.x, 0.0f};
        obj.text.hAlign = hAlign;
        obj.text.vAlign = vAlign;
        objects.emplace(obj.getId(), std::move(obj));
        ++stats.texts;
    };

    // 해치 객체: 바깥 경계(넓이가 가장 큰 루프)를 점으로 (피킹 · 범위), 무늬 선분은 지금 만들어 둔다.
    // o / right / up 은 해치 평면 (월드). 평면 좌표는 객체 원점 기준 로컬로 옮긴다.
    auto addHatch = [&](std::shared_ptr<lot_hatch::HatchData> h, const vec3& o, const vec3& right, const vec3& up,
                        const Entity& e) {
        const uint32_t layerId = layerOf(e);
        bool byLayer = false;
        const vec3 color = colorOf(e, layerId, byLayer);
        h->origin = vec3{0.0f, 0.0f, 0.0f};
        h->right = right;
        h->up = up;
        auto obj = LotGameObject::createGameObject();
        obj.transform.translation = o;
        obj.color = color;
        obj.colorByLayer = byLayer;
        obj.layer = layerId;
        obj.linetype = lot_linetype::kByLayer;
        lot_hatch::attach(obj, std::move(h));
        objects.emplace(obj.getId(), std::move(obj));
        ++stats.hatches;
    };

    // 메시 조각 (월드 삼각형, 세 점씩). 바로 앞 객체가 같은 층 · 색의 메시면 거기에 붙인다 -
    // 3ds Max 같은 것은 면마다 3DFACE 를 써서 수만 객체가 되기 때문 (네이티브 import 와 같다).
    // 모델은 부르는 쪽이 stats.meshTriangles 로 만든다.
    struct MeshKey {
        uint32_t layer;
        bool byLayer;
        vec3 color;
    };
    MeshKey lastMeshKey{};
    size_t lastMeshObjects = static_cast<size_t>(-1);   // 그때의 objects.size() - 그새 다른 것이 생겼으면 다르다
    auto addMesh = [&](const std::vector<vec3>& tris, const Entity& e) {
        if (tris.size() < 3) return;
        const uint32_t layerId = layerOf(e);
        bool byLayer = false;
        const vec3 color = colorOf(e, layerId, byLayer);
        stats.faces += static_cast<int>(tris.size() / 3);
        if (!stats.meshTriangles.empty() && lastMeshObjects == objects.size() && lastMeshKey.layer == layerId
            && lastMeshKey.byLayer == byLayer && lastMeshKey.color.x == color.x && lastMeshKey.color.y == color.y
            && lastMeshKey.color.z == color.z) {
            auto& last = stats.meshTriangles.back();
            const vec3 o = objects.at(last.first).transform.translation;
            for (const vec3& p : tris) last.second.push_back(p - o);
            return;
        }
        vec3 mn = tris[0], mx = tris[0];
        for (const vec3& p : tris) {
            mn = vec3{std::fmin(mn.x, p.x), std::fmin(mn.y, p.y), std::fmin(mn.z, p.z)};
            mx = vec3{std::fmax(mx.x, p.x), std::fmax(mx.y, p.y), std::fmax(mx.z, p.z)};
        }
        auto obj = LotGameObject::createGameObject();
        obj.transform.translation = (mn + mx) * 0.5f;
        obj.color = color;
        obj.colorByLayer = byLayer;
        obj.layer = layerId;
        std::vector<vec3> local;
        local.reserve(tris.size());
        for (const vec3& p : tris) local.push_back(p - obj.transform.translation);
        stats.meshTriangles.emplace_back(obj.getId(), std::move(local));
        objects.emplace(obj.getId(), std::move(obj));
        ++stats.meshes;
        lastMeshObjects = objects.size();
        lastMeshKey = MeshKey{layerId, byLayer, color};
    };
    // 다각형 면 (정점 번호 목록) -> 삼각형 팬. 번호가 범위 밖이면 그 면은 버린다 (네이티브 appendFace).
    auto appendFace = [](std::vector<vec3>& tris, const std::vector<vec3>& v, const std::vector<int>& idx,
                         const Xform& x) {
        if (idx.size() < 3) return;
        for (int i : idx) if (i < 0 || i >= static_cast<int>(v.size())) return;
        for (size_t k = 1; k + 1 < idx.size(); ++k) {
            tris.push_back(x.point(v[idx[0]]));
            tris.push_back(x.point(v[idx[k]]));
            tris.push_back(x.point(v[idx[k + 1]]));
        }
    };

    // ---- 1. 훑기: 엔티티와 블록 정의를 모은다 ----
    //
    // 블록 안의 것은 블록 좌표라 바로 그리면 안 된다 - INSERT 가 놓는 자리로 옮겨야 한다.
    // 그래서 먼저 다 모아 두고 (2) 에서 펼친다. POLYLINE ... VERTEX ... SEQEND 묶음은
    // 여기서 LWPOLYLINE 모양 하나로 접어 둔다 - 펼치는 쪽이 한 가지만 알면 된다.
    std::vector<Entity> modelEntities;
    std::unordered_map<std::string, Block> blocks;
    std::string section;
    std::string blockName;       // 지금 정의 중인 블록 (BLOCKS 섹션)
    bool inBlock = false;

    auto store = [&](Entity e) {
        if (section == "ENTITIES") {
            shiftEntity(e, stats.originX, stats.originY);
            modelEntities.push_back(std::move(e));
        }
        else if (section == "BLOCKS" && inBlock) blocks[blockName].entities.push_back(std::move(e));
    };

    Reader reader{text};
    int code = 0;
    std::string value;
    Entity current;
    bool inEntity = false;
    bool inPolyline = false;
    Entity polyline;             // 머리 + 접어 넣는 꼭짓점들
    bool polylineFirstVertex = true;

    auto finishEntity = [&]() {
        if (!inEntity) return;
        Entity& e = current;
        const std::string& t = e.type;

        if (t == "LAYER" && section == "TABLES") {
            const std::string name = e.str(2);
            if (name.empty()) return;
            const int aci = e.integer(62, 7);
            const vec3 color = aciColor(std::abs(aci));
            uint32_t id;
            if (name == "0") {
                id = LotLayers::kDefault;
            } else {
                id = layers.create(name, color);
                layerIds.emplace(name, id);
            }
            if (LotLayers::Layer* l = layers.find(id)) {
                l->color = color;
                l->linetype = linetypeByName(e.str(6));
                if (id != LotLayers::kDefault) {
                    l->visible = aci >= 0;            // 음수 = 꺼진 층 (DXF 관례)
                    l->locked = (e.integer(70) & 4) != 0;
                }
            }
            ++stats.layers;
            return;
        }
        if (section == "BLOCKS" && t == "BLOCK") {
            blockName = e.str(2);
            inBlock = !blockName.empty();
            if (inBlock) blocks[blockName].base = vec3{e.num(10), e.num(20), e.num(30)};
            return;
        }
        if (section == "BLOCKS" && t == "ENDBLK") { inBlock = false; return; }
        if (t.empty() || t == "SECTION" || t == "ENDSEC" || t == "EOF" || t == "SEQEND") return;
        if (section != "ENTITIES" && section != "BLOCKS") return;
        store(std::move(e));
    };

    while (reader.next(code, value)) {
        if (code == 0) {
            if (inPolyline) {
                if (current.type == "VERTEX" && polyline.type == "PMESH") {
                    // 메시 POLYLINE: 정점은 10/20/30 그대로, 폴리페이스 면 기록(128 만, 64 없음)은
                    // 71..74 (1 기준, 음수 = 숨은 모서리) 넷씩 seq 에 (없는 자리는 0)
                    const int vf = current.integer(70);
                    if ((vf & 128) && !(vf & 64)) {
                        for (int k = 71; k <= 74; ++k) polyline.seq.emplace_back(k, current.str(k, "0"));
                    } else {
                        polyline.values.emplace(10, current.str(10, "0"));
                        polyline.values.emplace(20, current.str(20, "0"));
                        polyline.values.emplace(30, current.str(30, "0"));
                    }
                    inEntity = false;
                } else if (current.type == "VERTEX") {
                    // 폴리페이스 메시의 면 기록(128 만, 64 없음)은 좌표가 아니다
                    const int vf = current.integer(70);
                    if (!((vf & 128) && !(vf & 64))) {
                        polyline.values.emplace(10, current.str(10, "0"));
                        polyline.values.emplace(20, current.str(20, "0"));
                        polyline.values.emplace(42, current.str(42, "0"));
                        if (polylineFirstVertex) polyline.values.emplace(38, current.str(30, "0"));
                        polylineFirstVertex = false;
                    }
                    inEntity = false;
                } else if (current.type == "POLYLINE") {
                    // 폴리페이스(64) · 폴리곤 메시(16) 는 선이 아니라 면이다 (네이티브 isMesh)
                    if (current.integer(70) & (16 | 64)) polyline.type = "PMESH";
                    for (const auto& kv : current.values) {
                        if (kv.first != 10 && kv.first != 20 && kv.first != 30) polyline.values.emplace(kv);
                    }
                    inEntity = false;
                }
                if (value == "SEQEND") {
                    inPolyline = false;
                    current = std::move(polyline);
                    inEntity = true;
                    finishEntity();
                    inEntity = false;
                    polyline = Entity{};
                }
            }
            finishEntity();

            if (value == "SECTION" || value == "ENDSEC") section.clear();
            if (value == "POLYLINE") {
                inPolyline = true;
                polyline = Entity{};
                polyline.type = "LWPOLYLINE";
                polylineFirstVertex = true;
            }
            current = Entity{};
            current.type = value;
            inEntity = true;
            if (value == "EOF") break;
        } else if (code == 2 && current.type == "SECTION") {
            section = value;              // HEADER / TABLES / BLOCKS / ENTITIES
            current.values.emplace(code, value);
        } else {
            current.values.emplace(code, value);
            if (current.type == "HATCH") current.seq.emplace_back(code, value);
        }
    }

    // ---- 2. 펼치기 ----
    //
    // parent 는 이 엔티티를 놓은 INSERT (맨 바깥이면 nullptr). 블록 안에서 층 "0" 인 것은
    // 삽입한 층을, 색 BYBLOCK(0) 은 삽입의 색을 따른다 (AutoCAD 규칙).
    std::function<void(const Entity&, const Xform&, const Entity*, int)> emit;
    int inserts = 0;

    auto expandBlock = [&](const std::string& name, const Xform& placed, const Entity& owner, int depth) {
        auto it = blocks.find(name);
        if (it == blocks.end() || depth > 16) return;
        Xform toBase;
        toBase.t = it->second.base * -1.0f;
        const Xform x = placed.then(toBase);
        for (const Entity& child : it->second.entities) emit(child, x, &owner, depth + 1);
    };

    emit = [&](const Entity& src, const Xform& x, const Entity* parent, int depth) {
        // 블록 안 엔티티의 층/색을 삽입한 쪽에서 물려받는다
        Entity inherited;
        const Entity* ep = &src;
        if (parent && (src.str(8, "0") == "0" || src.integer(62, 256) == 0)) {
            inherited = src;
            if (src.str(8, "0") == "0") { inherited.values.erase(8); inherited.values.emplace(8, parent->str(8, "0")); }
            if (src.integer(62, 256) == 0) {
                inherited.values.erase(62);
                if (parent->has(62)) inherited.values.emplace(62, parent->str(62));
            }
            ep = &inherited;
        }
        const Entity& e = *ep;
        const std::string& t = e.type;

        // OCS: 2D 엔티티(원·호·폴리선·문자·삽입)는 돌출 방향(210/220/230) 기준 좌표다.
        // 실제 도면에서 만나는 것은 거의 (0,0,-1) - 거울 복사한 것 - 이고, 그때 월드는
        // (-x, y, -z) 다 (임의 축 알고리즘). LINE/MTEXT/SPLINE 은 월드 좌표라 해당 없다.
        if (e.num(230, 1.0f) < 0.0f && t != "LINE" && t != "MTEXT" && t != "SPLINE" && t != "DIMENSION"
            && t != "ELLIPSE" && t != "LEADER" && t != "ACAD_TABLE"
            && t != "3DFACE" && t != "MESH" && t != "PMESH"   // 월드 좌표 (OCS 아님)
            && std::fabs(e.num(210)) < 1.0f / 64.0f && std::fabs(e.num(220)) < 1.0f / 64.0f) {
            Entity flat = e;
            flat.values.erase(230);
            Xform mirror;
            mirror.a = -1.0f;
            emit(flat, x.then(mirror), parent, depth);
            return;
        }

        if (t == "LINE") {
            addSketch({x.point(vec3{e.num(10), e.num(20), e.num(30)}),
                       x.point(vec3{e.num(11), e.num(21), e.num(31)})}, false, e, nullptr);
            ++stats.lines;
        } else if (t == "CIRCLE" || t == "ARC") {
            const bool isArc = (t == "ARC");
            LotGameObject::Curve c;
            c.kind = isArc ? LotGameObject::Curve::Kind::Arc : LotGameObject::Curve::Kind::Circle;
            c.center = vec3{e.num(10), e.num(20), e.num(30)};
            c.radius = e.num(40, 1.0f);
            c.right = vec3{1.0f, 0.0f, 0.0f};
            c.up = vec3{0.0f, 1.0f, 0.0f};
            c.start = isArc ? e.num(50) * kDegToRad : 0.0f;
            c.end = isArc ? e.num(51) * kDegToRad : 2.0f * kPi;
            if (isArc && c.end <= c.start) c.end += 2.0f * kPi;  // DXF 호는 항상 반시계
            std::vector<vec3> pts = tessellateArc(c.center, c.radius, c.right, c.up, c.start, c.end, isArc);
            for (vec3& p : pts) p = x.point(p);
            // 균등 축척(거울 아님)이면 곡선 정보를 지킨다 - 원/호 스냅과 내보내기가 산다
            if (x.conformal() && (x.det() > 0.0f || !isArc)) {
                c.center = x.point(c.center);
                c.radius *= x.scaleX();
                if (isArc) { c.start += x.rotation(); c.end += x.rotation(); }
                addSketch(std::move(pts), !isArc, e, &c);
            } else {
                addSketch(std::move(pts), !isArc, e, nullptr);
            }
            if (isArc) ++stats.arcs; else ++stats.circles;
        } else if (t == "LWPOLYLINE") {
            const std::vector<float> xs = e.all(10), ys = e.all(20);
            const std::vector<float> bulges = e.all(42);
            const float z = e.num(38);
            const bool closed = (e.integer(70) & 1) != 0;
            std::vector<vec3> pts;
            const size_t n = std::min(xs.size(), ys.size());
            for (size_t i = 0; i < n; ++i) {
                pts.push_back(vec3{xs[i], ys[i], z});
                // bulge 는 '이 점에서 다음 점까지' 다. 개수가 점과 같지 않은 파일도 있다.
                const size_t last = (i + 1 < n) ? i + 1 : 0;
                if ((i + 1 < n || closed) && i < bulges.size() && std::fabs(bulges[i]) > 1e-9f) {
                    appendBulgeArc(pts, vec3{xs[i], ys[i], z}, vec3{xs[last], ys[last], z}, bulges[i]);
                }
            }
            for (vec3& p : pts) p = x.point(p);
            addSketch(std::move(pts), closed, e, nullptr);
            ++stats.polylines;
        } else if (t == "TEXT" || t == "ATTRIB" || t == "ATTDEF") {
            // ATTRIB = 블록 속성 값. ATTDEF 는 정의라 상수(70 & 2)일 때만 보인다. 70 & 1 = 숨김.
            const int flags = e.integer(70);
            if ((t != "TEXT" && (flags & 1)) || (t == "ATTDEF" && !(flags & 2))) return;
            const std::string content = decodeTextCodes(e.str(1));
            if (content.empty()) { noteSkipped(t + "(empty)"); return; }
            const float angle = e.num(50) * kDegToRad;
            // 맞춤(72 가로 / 73·74 세로). 왼쪽 아래가 아니면 11/21/31 이 진짜 기준점이다.
            const int hDxf = e.integer(72, 0);
            const int vDxf = e.integer(t == "TEXT" ? 73 : 74, 0);
            const bool aligned = hDxf != 0 || vDxf != 0;
            const vec3 at = (aligned && e.has(11)) ? vec3{e.num(11), e.num(21), e.num(31)}
                                                   : vec3{e.num(10), e.num(20), e.num(30)};
            vec3 dir = x.vector(vec3{std::cos(angle), std::sin(angle), 0.0f});
            const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            dir = (dl > 0.0f) ? dir * (1.0f / dl) : vec3{1.0f, 0.0f, 0.0f};
            // DXF 73: 0 기준선 · 1 아래 · 2 가운데 · 3 위 -> 우리 0 기준선 · 1 가운데 · 2 위
            addText(content, e, x.point(at), dir, e.num(40, 1.0f) * x.scaleY(),
                    (hDxf == 4) ? 1 : (hDxf >= 0 && hDxf <= 2) ? hDxf : 0,
                    (vDxf == 2 || hDxf == 4) ? 1 : (vDxf == 3) ? 2 : 0);
        } else if (t == "MTEXT") {
            // 본문은 250 자씩 3 에 나뉘고 마지막이 1 이다
            std::string raw;
            auto range = e.values.equal_range(3);
            for (auto it = range.first; it != range.second; ++it) raw += it->second;
            raw += e.str(1);
            std::vector<std::string> lines = mtextLines(raw);
            while (!lines.empty() && lines.back().empty()) lines.pop_back();
            if (lines.empty()) { noteSkipped("MTEXT(empty)"); return; }

            vec3 dir = e.has(11) ? vec3{e.num(11), e.num(21), 0.0f}
                                 : vec3{std::cos(e.num(50) * kDegToRad), std::sin(e.num(50) * kDegToRad), 0.0f};
            dir = x.vector(dir);
            const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            dir = (dl > 0.0f) ? dir * (1.0f / dl) : vec3{1.0f, 0.0f, 0.0f};
            const vec3 up{-dir.y, dir.x, 0.0f};
            const float height = e.num(40, 1.0f) * x.scaleY();
            // 71: 1 위왼 2 위가운데 3 위오른 / 4..6 가운데 / 7..9 아래
            const int attach = std::clamp(e.integer(71, 1), 1, 9);
            const int hAlign = (attach - 1) % 3;
            const int row = (attach - 1) / 3;
            const float step = height * 1.666f * e.num(44, 1.0f);   // 줄 간격 (AutoCAD 기본)
            const float n1 = static_cast<float>(lines.size() - 1);
            const vec3 at = x.point(vec3{e.num(10), e.num(20), e.num(30)});
            for (size_t i = 0; i < lines.size(); ++i) {
                const float fi = static_cast<float>(i);
                // 위 맞춤은 첫 줄이 기준점에, 아래 맞춤은 마지막 줄이, 가운데는 묶음 가운데가
                const float offset = (row == 0) ? -fi * step
                                   : (row == 1) ? (n1 * 0.5f - fi) * step
                                                : (n1 - fi) * step;
                addText(lines[i], e, at + up * offset, dir, height, hAlign,
                        (row == 0) ? 2 : (row == 1) ? 1 : 0);
            }
        } else if (t == "SPLINE") {
            // NURBS (차수 71, 매듭 40, 제어점 10/20/30, 가중치 41, 맞춤점 11/21/31) - 네이티브와 같다
            std::vector<vec3> P, fit;
            {
                const std::vector<float> cx = e.all(10), cy = e.all(20), cz = e.all(30);
                for (size_t i = 0; i < std::min(cx.size(), cy.size()); ++i) P.push_back(vec3{cx[i], cy[i], i < cz.size() ? cz[i] : 0.0f});
                const std::vector<float> fx = e.all(11), fy = e.all(21), fz = e.all(31);
                for (size_t i = 0; i < std::min(fx.size(), fy.size()); ++i) fit.push_back(vec3{fx[i], fy[i], i < fz.size() ? fz[i] : 0.0f});
            }
            std::vector<vec3> pts = tessellateSpline(P, e.all(41), e.all(40), e.integer(71, 3), fit);
            for (vec3& q : pts) q = x.point(q);
            if (pts.size() >= 2) {
                addSketch(std::move(pts), (e.integer(70) & 1) != 0, e, nullptr);
                ++stats.splines;
            } else {
                noteSkipped("SPLINE");
            }
        } else if (t == "HATCH") {
            // 네이티브 pushHatch 와 같다: 경계 경로 -> 무늬 정의. 실패해도 있는 데까지.
            // 맨 바깥 해치는 원점 이동(shiftEntity)에서 빠졌다 - 여기서 해치 통째로 옮긴다.
            Xform hx = x;
            if (!parent && (stats.originX != 0.0 || stats.originY != 0.0)) {
                Xform tr;
                tr.t = vec3{static_cast<float>(-stats.originX), static_cast<float>(-stats.originY), 0.0f};
                hx = tr.then(x);
            }
            RawCursor cur(e.seq);
            std::string v;
            float elev = 0.0f;
            if (cur.find(30, v, 60)) elev = toFloat(v);
            std::string pattern = "ANSI31";
            { RawCursor c2(e.seq); if (c2.find(2, v, 60)) pattern = v; }
            const bool solid = [&] { RawCursor c2(e.seq); return (c2.integer(70, 0) & 1) != 0; }();
            const int nLoops = cur.integer(91, 0);
            auto h = std::make_shared<lot_hatch::HatchData>();
            h->solid = solid;
            h->patternName = pattern;
            for (int li = 0; li < nLoops; ++li) {
                if (!cur.find(92, v)) break;
                const int flags = toInt(v);
                std::vector<vec3> pts;
                if (flags & 2) {   // 폴리선 경로: 72 bulge 유무, 73 닫힘(무시 - 늘 닫는다), 93 정점 수, 10/20[/42]
                    const int hasBulge = cur.integer(72, 0);
                    cur.integer(73, 1);
                    const int nv = cur.integer(93, 0);
                    std::vector<vec3> vtx;
                    std::vector<float> bul;
                    for (int k = 0; k < nv; ++k) {
                        const float px = cur.num(10), py = cur.num(20);
                        vtx.push_back(vec3{px, py, elev});
                        bul.push_back(hasBulge && cur.peek(42) ? cur.num(42) : 0.0f);
                    }
                    for (size_t k = 0; k < vtx.size(); ++k) {
                        pts.push_back(vtx[k]);
                        if (std::fabs(bul[k]) > 1e-9f) appendBulgeArc(pts, vtx[k], vtx[(k + 1) % vtx.size()], bul[k]);
                    }
                } else {           // 모서리 경로: 93 개수, 모서리마다 72 종류
                    const int ne = cur.integer(93, 0);
                    for (int k = 0; k < ne; ++k) {
                        const int type = cur.integer(72, 1);
                        if (type == 1) {
                            const float x0 = cur.num(10), y0 = cur.num(20), x1 = cur.num(11), y1 = cur.num(21);
                            pts.push_back(vec3{x0, y0, elev});
                            pts.push_back(vec3{x1, y1, elev});
                        } else if (type == 2 || type == 3) {
                            const float cx = cur.num(10), cy = cur.num(20);
                            float mx = 1.0f, my = 0.0f, ratio = 1.0f;
                            if (type == 3) { mx = cur.num(11); my = cur.num(21); ratio = cur.num(40, 1.0f); }
                            const float r = (type == 2) ? cur.num(40, 1.0f) : 1.0f;
                            float a0 = cur.num(50) * kDegToRad, a1 = cur.num(51) * kDegToRad;
                            const bool ccw = cur.integer(73, 1) != 0;
                            if (ccw) { if (a1 <= a0) a1 += 2.0f * kPi; } else { if (a1 >= a0) a1 -= 2.0f * kPi; }
                            const int seg = std::max(4, static_cast<int>(std::ceil(std::fabs(a1 - a0) / (2.0f * kPi) * 48.0f)));
                            for (int s = 0; s <= seg; ++s) {
                                const float a = a0 + (a1 - a0) * static_cast<float>(s) / static_cast<float>(seg);
                                if (type == 2) pts.push_back(vec3{cx + r * std::cos(a), cy + r * std::sin(a), elev});
                                else pts.push_back(vec3{cx + mx * std::cos(a) - my * ratio * std::sin(a),
                                                        cy + my * std::cos(a) + mx * ratio * std::sin(a), elev});
                            }
                        } else if (type == 4) {
                            const int degree = cur.integer(94, 3);
                            cur.integer(73, 0);
                            cur.integer(74, 0);
                            const int nk = cur.integer(95, 0), nc = cur.integer(96, 0);
                            std::vector<float> knots, weights;
                            std::vector<vec3> P, fit;
                            for (int s = 0; s < nk; ++s) knots.push_back(cur.num(40));
                            for (int s = 0; s < nc; ++s) {
                                const float px = cur.num(10), py = cur.num(20);
                                P.push_back(vec3{px, py, elev});
                                if (cur.peek(42)) weights.push_back(cur.num(42));
                            }
                            const int nf = cur.integer(97, 0);
                            for (int s = 0; s < nf; ++s) {
                                const float px = cur.num(11), py = cur.num(21);
                                fit.push_back(vec3{px, py, elev});
                            }
                            const std::vector<vec3> sp = tessellateSpline(P, weights, knots, degree, fit);
                            pts.insert(pts.end(), sp.begin(), sp.end());
                        } else {
                            break;
                        }
                    }
                }
                // 경계 객체 참조 (97 + 330 x n) 는 건너뛴다
                { const int ns = cur.integer(97, 0); for (int s = 0; s < ns; ++s) cur.find(330, v, 4); }
                std::vector<lot_hatch::P2> loop;
                for (const vec3& q : pts) {
                    const lot_hatch::P2 p2{q.x, q.y};
                    if (loop.empty() || std::hypot(loop.back().x - p2.x, loop.back().y - p2.y) > 1e-6f) loop.push_back(p2);
                }
                if (loop.size() > 2 && std::hypot(loop.front().x - loop.back().x, loop.front().y - loop.back().y) < 1e-6f) loop.pop_back();
                if (loop.size() >= 3) h->loops.push_back(std::move(loop));
            }
            if (h->loops.empty()) { noteSkipped("HATCH(no boundary)"); return; }
            if (!solid) {
                // 무늬 정의 (파일 값: 축척 · 회전 적용됨). 없으면 이름으로 내장, 그도 없으면 ANSI31.
                h->angleDeg = cur.num(52, 0.0f);
                h->scale = cur.num(41, 1.0f);
                const int nLines = cur.integer(78, 0);
                for (int k = 0; k < nLines; ++k) {
                    lot_hatch::PatternLine L;
                    L.angleDeg = cur.num(53);
                    L.base = {cur.num(43), cur.num(44)};
                    L.offset = {cur.num(45), cur.num(46)};
                    const int nd = cur.integer(79, 0);
                    for (int s = 0; s < nd; ++s) L.dashes.push_back(cur.num(49));
                    h->lines.push_back(std::move(L));
                }
                if (h->lines.empty()) {
                    std::vector<lot_hatch::PatternLine> def;
                    if (!lot_hatch::builtinPattern(pattern, def)) lot_hatch::builtinPattern("ANSI31", def);
                    h->lines = lot_hatch::transformPattern(def, h->scale, h->angleDeg);
                }
            }
            // 평면 -> 월드 (블록 변환 포함). 평면 안 좌표는 |오른쪽 축| 로 균등 축척.
            const vec3 o = hx.point(vec3{0.0f, 0.0f, elev});
            const vec3 rv = hx.vector(vec3{1.0f, 0.0f, 0.0f}), uv = hx.vector(vec3{0.0f, 1.0f, 0.0f});
            const float lr = std::sqrt(dot(rv, rv)), lu = std::sqrt(dot(uv, uv));
            if (lr < 1e-12f || lu < 1e-12f) return;
            if (std::fabs(lr - 1.0f) > 1e-6f) {
                for (auto& lp : h->loops) for (auto& q : lp) q = q * lr;
                for (auto& L : h->lines) { L.base = L.base * lr; L.offset = L.offset * lr; for (auto& dd : L.dashes) dd *= lr; }
                h->scale *= lr;
            }
            addHatch(h, o, rv * (1.0f / lr), uv * (1.0f / lu), e);
        } else if (t == "ELLIPSE") {
            // 네이티브와 같다: 짧은 축 = (돌출 방향 x 장축) * 비율, 매개변수 41/42 (라디안), 64 분할/바퀴
            const vec3 c{e.num(10), e.num(20), e.num(30)};
            const vec3 m{e.num(11), e.num(21), e.num(31)};
            vec3 nrm{e.num(210), e.num(220), e.num(230, 1.0f)};
            const float nl = std::sqrt(dot(nrm, nrm));
            nrm = nl > 1e-9f ? nrm * (1.0f / nl) : vec3{0.0f, 0.0f, 1.0f};
            const vec3 minor = cross(nrm, m) * e.num(40, 1.0f);
            float t0 = e.num(41, 0.0f), t1 = e.num(42, 2.0f * kPi);
            if (t1 <= t0) t1 += 2.0f * kPi;
            const float span = t1 - t0;
            const bool full = span >= 2.0f * kPi - 1e-4f;
            const int seg = std::max(8, static_cast<int>(std::ceil(span / (2.0f * kPi) * 64.0f)));
            std::vector<vec3> pts;
            for (int i = 0; i <= seg; ++i) {
                if (full && i == seg) break;   // 닫힌 타원은 겹친 끝점을 버린다
                const float a = t0 + span * static_cast<float>(i) / static_cast<float>(seg);
                pts.push_back(x.point(c + m * std::cos(a) + minor * std::sin(a)));
            }
            addSketch(std::move(pts), full, e, nullptr);
            ++stats.ellipses;
        } else if (t == "LEADER") {
            // 꼭짓점들을 잇는 열린 폴리선 (화살촉 없음 - 네이티브와 같다)
            const std::vector<float> lx = e.all(10), ly = e.all(20), lz = e.all(30);
            std::vector<vec3> pts;
            for (size_t i = 0; i < std::min(lx.size(), ly.size()); ++i) pts.push_back(x.point(vec3{lx[i], ly[i], i < lz.size() ? lz[i] : 0.0f}));
            if (pts.size() >= 2) { addSketch(std::move(pts), false, e, nullptr); ++stats.leaders; }
            else noteSkipped("LEADER");
        } else if (t == "ACAD_TABLE") {
            // 표: 딸린 익명 블록(코드 2)을 삽입점만큼 옮겨 펼친다 (회전 · 축척은 없다 - 네이티브와 같다)
            Xform local;
            local.t = vec3{e.num(10), e.num(20), e.num(30)};
            if (blocks.count(e.str(2))) { expandBlock(e.str(2), x.then(local), e, depth); ++stats.tables; }
            else noteSkipped("ACAD_TABLE");
        } else if (t == "3DFACE") {
            // 면 - 3D 로 내보낸 DXF(3ds Max · Rhino 등)는 3DFACE 가 곧 메시다. 넷째 점이 셋째와 같으면 삼각형.
            std::vector<vec3> v;
            for (int k = 0; k < 4; ++k) {
                if (e.has(10 + k)) v.push_back(vec3{e.num(10 + k), e.num(20 + k), e.num(30 + k)});
            }
            if (v.size() == 4 && dot(v[3] - v[2], v[3] - v[2]) < 1e-24f) v.pop_back();
            std::vector<int> idx;
            for (int k = 0; k < static_cast<int>(v.size()); ++k) idx.push_back(k);
            std::vector<vec3> tris;
            appendFace(tris, v, idx, x);
            if (tris.empty()) noteSkipped("3DFACE"); else addMesh(tris, e);
        } else if (t == "MESH") {
            // AcDbSubDMesh: 정점 10/20/30, 면 목록 93 개의 90 값 [n, i…] (0 기준). 세분(91)은 무시하고
            // 제어 메시를 그대로 그린다 (네이티브와 같다).
            const std::vector<float> vx = e.all(10), vy = e.all(20), vz = e.all(30);
            std::vector<vec3> v;
            for (size_t i = 0; i < std::min(vx.size(), vy.size()); ++i) v.push_back(vec3{vx[i], vy[i], i < vz.size() ? vz[i] : 0.0f});
            std::vector<int> fl;
            for (float f : e.all(90)) fl.push_back(static_cast<int>(f));
            const int m = e.integer(93, 0);
            const size_t len = std::min(fl.size(), m > 0 ? static_cast<size_t>(m) : fl.size());
            std::vector<vec3> tris;
            for (size_t i = 0; i < len;) {
                const int n = fl[i];
                if (n <= 0 || i + 1 + n > len) break;
                appendFace(tris, v, std::vector<int>(fl.begin() + i + 1, fl.begin() + i + 1 + n), x);
                i += 1 + n;
            }
            if (tris.empty()) noteSkipped("MESH(no faces)"); else addMesh(tris, e);
        } else if (t == "PMESH") {
            // 메시 POLYLINE - 폴리페이스(64): 면 기록 71..74 (1 기준, 음수 = 숨은 모서리, 0 = 없음).
            // 폴리곤 메시(16): M(71) x N(72) 격자, 70 & 1 / & 32 = M / N 방향 닫힘.
            const std::vector<float> vx = e.all(10), vy = e.all(20), vz = e.all(30);
            std::vector<vec3> v;
            for (size_t i = 0; i < std::min(vx.size(), vy.size()); ++i) v.push_back(vec3{vx[i], vy[i], i < vz.size() ? vz[i] : 0.0f});
            const int flags = e.integer(70);
            std::vector<vec3> tris;
            if (flags & 64) {
                for (size_t i = 0; i + 3 < e.seq.size(); i += 4) {
                    std::vector<int> idx;
                    for (size_t k = 0; k < 4; ++k) {
                        const int f = toInt(e.seq[i + k].second);
                        if (f != 0) idx.push_back(std::abs(f) - 1);
                    }
                    appendFace(tris, v, idx, x);
                }
            } else {
                const int mM = e.integer(71), mN = e.integer(72);
                if (mM > 0 && mN > 0 && static_cast<int>(v.size()) >= mM * mN) {
                    const bool closeM = (flags & 1) != 0, closeN = (flags & 32) != 0;
                    const int mEnd = closeM ? mM : mM - 1, nEnd = closeN ? mN : mN - 1;
                    for (int i = 0; i < mEnd; ++i) {
                        for (int j = 0; j < nEnd; ++j) {
                            const int i1 = (i + 1) % mM, j1 = (j + 1) % mN;
                            appendFace(tris, v, {i * mN + j, i1 * mN + j, i1 * mN + j1, i * mN + j1}, x);
                        }
                    }
                }
            }
            if (tris.empty()) noteSkipped("POLYLINE(mesh)"); else addMesh(tris, e);
        } else if (t == "SOLID" || t == "TRACE") {
            // 채운 사각형 - 테두리만 그린다
            std::vector<vec3> pts{x.point(vec3{e.num(10), e.num(20), e.num(30)}),
                                  x.point(vec3{e.num(11), e.num(21), e.num(31)}),
                                  x.point(vec3{e.num(13), e.num(23), e.num(33)}),
                                  x.point(vec3{e.num(12), e.num(22), e.num(32)})};
            addSketch(std::move(pts), true, e, nullptr);
            ++stats.polylines;
        } else if (t == "INSERT") {
            // 블록 놓기: 기준점 10, 축척 41/42, 회전 50, 배열 70 열 × 71 행 (간격 44/45)
            const float sx = e.num(41, 1.0f), sy = e.num(42, 1.0f);
            const float rot = e.num(50) * kDegToRad;
            const float cs = std::cos(rot), sn = std::sin(rot);
            const int cols = std::max(1, e.integer(70, 1)), rows = std::max(1, e.integer(71, 1));
            const float dc = e.num(44), dr = e.num(45);
            for (int r = 0; r < rows && r < 100; ++r) {
                for (int cI = 0; cI < cols && cI < 100; ++cI) {
                    Xform local;
                    local.a = cs * sx;  local.b = -sn * sy;
                    local.c = sn * sx;  local.d = cs * sy;
                    // 배열 간격은 블록 회전 축을 따라
                    const float ox = cI * dc, oy = r * dr;
                    local.t = vec3{e.num(10) + cs * ox - sn * oy, e.num(20) + sn * ox + cs * oy, e.num(30)};
                    expandBlock(e.str(2), x.then(local), e, depth);
                }
            }
            ++inserts;
        } else if (t == "DIMENSION") {
            // 치수의 그려진 모양 (선 · 화살표 · 글자) 은 익명 블록(*D..)에 들어 있다
            // 블록은 월드 좌표로 그려져 있다 - 맨 바깥이면 도면을 옮긴 만큼만 따라 옮긴다
            Xform local;
            if (!parent) local.t = vec3{static_cast<float>(-stats.originX), static_cast<float>(-stats.originY), 0.0f};
            if (blocks.count(e.str(2))) expandBlock(e.str(2), x.then(local), e, depth);
            else noteSkipped("DIMENSION");
        } else if (t == "VERTEX" || t == "POLYLINE" || t == "VPORT" || t == "LTYPE" || t == "STYLE"
                   || t == "APPID" || t == "DIMSTYLE" || t == "UCS" || t == "VIEW" || t == "CLASS"
                   || t == "DICTIONARY" || t == "XRECORD" || t == "VISUALSTYLE" || t == "VIEWPORT"
                   || t == "SCALE" || t == "DICTIONARYVAR" || t == "LAYOUT" || t == "TABLE"
                   || t == "ENDTAB" || t == "MLINESTYLE" || t == "PLOTSETTINGS" || t == "TABLESTYLE") {
            // 구조용 항목 - 세지 않는다
        } else if (!t.empty()) {
            noteSkipped(t);
        }
    };

    for (const Entity& e : modelEntities) emit(e, Xform{}, nullptr, 0);

    const int total = stats.lines + stats.circles + stats.arcs + stats.polylines
                    + stats.texts + stats.splines + stats.hatches + stats.ellipses + stats.leaders
                    + stats.meshes;
    if (total == 0) {
        stats.error = "dxf: no drawable entities found (is this an ASCII DXF?)";
        return stats;
    }
    LOT_LOG("dxf: " << stats.lines << " lines, " << stats.circles << " circles, "
            << stats.arcs << " arcs, " << stats.polylines << " polylines, "
            << stats.texts << " texts, " << stats.splines << " splines (approx), "
            << stats.layers << " layers, " << inserts << " inserts, " << blocks.size() << " blocks, "
            << stats.hatches << " hatches, " << stats.ellipses << " ellipses, " << stats.leaders << " leaders, "
            << stats.tables << " tables, " << stats.meshes << " meshes (" << stats.faces << " triangles)"
            << (stats.skipped ? ", skipped " + std::to_string(stats.skipped) + " ("
                                + stats.skippedKinds + ")" : ""));
    if (stats.originX != 0.0 || stats.originY != 0.0) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "dxf: far from the origin - shifted by (%.0f, %.0f)", -stats.originX, -stats.originY);
        LOT_LOG(buf);
    }
    return stats;
}

// ============================================================== 쓰기 (R12 ASCII)
namespace {

// DXF 는 줄 두 개가 한 쌍이다: 그룹 코드, 값. 쓰는 쪽도 그 쌍만 있으면 된다.
struct Writer {
    std::string out;
    double originX = 0.0, originY = 0.0;   // 읽을 때 옮긴 만큼 되돌린다 (LoadStats::originX)

    void pair(int code, const std::string& v) {
        out += std::to_string(code);
        out += '\n';
        out += v;
        out += '\n';
    }
    void pair(int code, const char* v) { pair(code, std::string(v)); }
    void integer(int code, int v) { pair(code, std::to_string(v)); }
    void real(int code, float v) { pair(code, number(v)); }
    void point(int base, const vec3& p) {
        pair(base, number(static_cast<double>(p.x) + originX));
        pair(base + 10, number(static_cast<double>(p.y) + originY));
        real(base + 20, p.z);
    }

    // 고정 소수점. %g 는 큰 좌표에서 지수 표기(1e+06)로 새는데 그걸 못 읽는
    // 프로그램이 있다. 뒤따르는 0 만 털어 파일이 붓지 않게 한다.
    static std::string number(double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        std::string s(buf);
        const size_t dot = s.find('.');
        if (dot != std::string::npos) {
            size_t end = s.size();
            while (end > dot + 2 && s[end - 1] == '0') --end;
            s.erase(end);
        }
        return s;
    }
};

// RGB -> 가장 가까운 ACI. 읽을 때 쓰는 aciColor 를 그대로 되짚으므로,
// DXF 에서 읽어 온 색은 같은 번호로 되돌아간다 (왕복이 정확하다).
int aciFromColor(const vec3& c) {
    int best = 7;
    float bestDistance = 1e30f;
    for (int i = 1; i <= 255; ++i) {
        const vec3 a = aciColor(i);
        const vec3 d = a - c;
        const float distance = dot(d, d);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

float lengthOf(const vec3& v) { return std::sqrt(dot(v, v)); }

// 0 .. 360 으로 접는다 (DXF 호 각도 규약).
float degrees360(float radians) {
    float d = radians / kDegToRad;
    while (d < 0.0f) d += 360.0f;
    while (d >= 360.0f) d -= 360.0f;
    return d;
}

}  // namespace

std::string save(const LotGameObject::Map& objects, const LotLayers& layers,
                 float linetypeScale, SaveStats* outStats, double originX, double originY) {
    SaveStats st;
    Writer w;
    w.originX = originX;
    w.originY = originY;

    // 층은 이름으로 잇는다. 이름은 손대지 않는다 - R12 는 대문자만 허용하지만
    // 요즘 프로그램은 다 받고, 여기서 바꾸면 한글 층 이름이 왕복하지 못한다.
    std::unordered_map<uint32_t, std::string> layerNames;
    for (const LotLayers::Layer* l : layers.all()) layerNames.emplace(l->id, l->name);
    auto layerNameOf = [&](uint32_t id) -> std::string {
        auto it = layerNames.find(id);
        return it == layerNames.end() ? std::string("0") : it->second;
    };

    // 범위 ($EXTMIN/$EXTMAX). 읽는 쪽이 전체 보기를 바로 잡을 수 있게.
    vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool anyPoint = false;
    auto grow = [&](const vec3& p) {
        anyPoint = true;
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    };

    // ---- 엔티티 본문을 먼저 만든다 (헤더에 적을 범위를 알아야 하므로) ----
    Writer body;

    auto attributes = [&](const char* type, const LotGameObject& o) {
        body.pair(0, type);
        body.pair(8, layerNameOf(o.layer));
        if (!o.colorByLayer) body.integer(62, aciFromColor(o.color));
        if (o.linetype != lot_linetype::kByLayer) body.pair(6, lot_linetype::name(o.linetype));
    };

    auto writeLine = [&](const LotGameObject& o, const vec3& a, const vec3& b) {
        attributes("LINE", o);
        body.point(10, a);
        body.point(11, b);
        grow(a);
        grow(b);
    };

    auto writeText = [&](const LotGameObject& o, const std::string& content, const vec3& origin,
                         const vec3& right, const vec3& up, float height, int hAlign, int vAlign) {
        if (content.empty()) return;
        attributes("TEXT", o);
        body.point(10, origin);
        body.real(40, height);
        body.pair(1, content);
        body.real(50, degrees360(std::atan2(right.y, right.x)));
        // 우리 vAlign: 0 기준선 · 1 가운데 · 2 위 -> DXF 73: 0 기준선 · 2 가운데 · 3 위
        const int dxfV = (vAlign == 1) ? 2 : (vAlign == 2) ? 3 : 0;
        if (hAlign != 0 || dxfV != 0) {
            body.integer(72, hAlign);
            body.integer(73, dxfV);
            body.point(11, origin);  // 맞춤점이 있으면 그게 진짜 기준점이다
        }
        grow(origin);
        grow(origin + right * (height * static_cast<float>(content.size())));
        grow(origin - up * height);
    };

    // 폴리선. 모든 점의 z 가 같으면 2D (고도 하나), 아니면 3D 폴리선으로.
    auto writePolyline = [&](const LotGameObject& o, const std::vector<vec3>& pts, bool closed) {
        bool flat = true;
        for (const vec3& p : pts) {
            if (std::fabs(p.z - pts[0].z) > 1e-6f) { flat = false; break; }
        }
        attributes("POLYLINE", o);
        body.integer(66, 1);                 // 정점이 뒤따른다 (R12 에서는 필수)
        body.integer(70, (closed ? 1 : 0) | (flat ? 0 : 8));
        body.real(10, 0.0f);
        body.real(20, 0.0f);
        body.real(30, flat ? pts[0].z : 0.0f);
        for (const vec3& p : pts) {
            body.pair(0, "VERTEX");
            body.pair(8, layerNameOf(o.layer));
            body.point(10, p);
            if (!flat) body.integer(70, 32);  // 3D 폴리선의 정점
            grow(p);
        }
        body.pair(0, "SEQEND");
        body.pair(8, layerNameOf(o.layer));
    };

    // id 순으로 쓴다 - 맵은 순서가 없어 그냥 돌면 저장할 때마다 파일이 뒤바뀐다.
    std::vector<LotGameObject::id_t> ids;
    ids.reserve(objects.size());
    for (const auto& entry : objects) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());

    for (const LotGameObject::id_t id : ids) {
        const LotGameObject& o = objects.at(id);
        const mat4 m = o.transform.mat4Transform();

        if (o.text.valid) {
            writeText(o, o.text.content, o.transform.translation,
                      normalize(rotate(o.transform.rotation, o.text.right)),
                      normalize(rotate(o.transform.rotation, o.text.up)),
                      o.text.height, o.text.hAlign, o.text.vAlign);
            ++st.texts;
            continue;
        }

        if (o.dim.valid) {
            // DIMENSION 엔티티는 그려진 모양을 담은 블록을 달고 다녀야 한다.
            // 보이는 것을 그대로 선과 글자로 풀어 쓴다 (측정값은 글자로 남는다).
            const lot_dim::Geometry g = lot_dim::build(o.dim, m, nullptr);
            for (const std::pair<vec3, vec3>& seg : g.segments) writeLine(o, seg.first, seg.second);
            writeText(o, g.text, g.textOrigin, g.textRight, g.textUp, g.textHeight, 1, 1);
            ++st.dimensions;
            continue;
        }

        if (o.points.size() < 2) {
            ++st.skipped;   // 메시 (큐브 · OBJ) - 2D 도면 엔티티가 아니다
            continue;
        }

        // 원/호는 평면에 누운 채 모양이 안 망가졌을 때만 진짜 CIRCLE/ARC 로 나간다.
        // 기울었거나 축마다 다르게 늘어났으면 타원이 되는데, 그건 잘라 둔 점으로 쓴다.
        if (o.curve.kind != LotGameObject::Curve::Kind::None) {
            const vec3 center = transformPoint(m, o.curve.center);
            const vec3 right = transformPoint(m, o.curve.center + o.curve.right * o.curve.radius) - center;
            const vec3 up = transformPoint(m, o.curve.center + o.curve.up * o.curve.radius) - center;
            const float rr = lengthOf(right), ru = lengthOf(up);
            const float tolerance = 1e-4f * std::fmax(rr, 1.0f);
            if (rr > 1e-6f && std::fabs(rr - ru) < tolerance
                && std::fabs(right.z) < tolerance && std::fabs(up.z) < tolerance) {
                if (o.curve.kind == LotGameObject::Curve::Kind::Circle) {
                    attributes("CIRCLE", o);
                    body.point(10, center);
                    body.real(40, rr);
                    ++st.circles;
                } else {
                    const vec3 a = right * std::cos(o.curve.start) + up * std::sin(o.curve.start);
                    const vec3 b = right * std::cos(o.curve.end) + up * std::sin(o.curve.end);
                    float start = degrees360(std::atan2(a.y, a.x));
                    float end = degrees360(std::atan2(b.y, b.x));
                    // DXF 호는 언제나 반시계다. 우리 호가 시계 방향이면 양 끝을 바꾼다.
                    if (right.x * up.y - right.y * up.x < 0.0f) std::swap(start, end);
                    attributes("ARC", o);
                    body.point(10, center);
                    body.real(40, rr);
                    body.real(50, start);
                    body.real(51, end);
                    ++st.arcs;
                }
                grow(center - vec3{rr, rr, 0.0f});
                grow(center + vec3{rr, rr, 0.0f});
                continue;
            }
        }

        std::vector<vec3> pts;
        pts.reserve(o.points.size());
        for (const vec3& p : o.points) pts.push_back(transformPoint(m, p));

        if (pts.size() == 2 && !o.closed) {
            writeLine(o, pts[0], pts[1]);
            ++st.lines;
        } else {
            writePolyline(o, pts, o.closed);
            ++st.polylines;
        }
    }

    if (!anyPoint) { lo = vec3{0.0f, 0.0f, 0.0f}; hi = vec3{0.0f, 0.0f, 0.0f}; }

    // ---- HEADER ----
    w.pair(0, "SECTION");
    w.pair(2, "HEADER");
    w.pair(9, "$ACADVER");   w.pair(1, "AC1009");
    w.pair(9, "$INSBASE");   w.point(10, vec3{0.0f, 0.0f, 0.0f});
    w.pair(9, "$EXTMIN");    w.point(10, lo);
    w.pair(9, "$EXTMAX");    w.point(10, hi);
    w.pair(9, "$LTSCALE");   w.real(40, linetypeScale);
    w.pair(9, "$CLAYER");    w.pair(8, layerNameOf(layers.current()));
    w.pair(0, "ENDSEC");

    // ---- TABLES: LTYPE, LAYER ----
    w.pair(0, "SECTION");
    w.pair(2, "TABLES");

    const std::vector<lot_linetype::Definition>& definitions = lot_linetype::standard();
    w.pair(0, "TABLE");
    w.pair(2, "LTYPE");
    w.integer(70, static_cast<int>(definitions.size()));
    for (const lot_linetype::Definition& d : definitions) {
        w.pair(0, "LTYPE");
        w.pair(2, d.name);
        w.integer(70, 0);
        w.pair(3, d.name);
        w.integer(72, 65);                      // 'A' - 정렬 방식, 언제나 이것
        w.integer(73, static_cast<int>(d.pattern.size()));
        float total = 0.0f;
        for (float dash : d.pattern) total += std::fabs(dash);
        w.real(40, total);
        for (float dash : d.pattern) w.real(49, dash);
    }
    w.pair(0, "ENDTAB");

    const std::vector<const LotLayers::Layer*> all = layers.all();
    w.pair(0, "TABLE");
    w.pair(2, "LAYER");
    w.integer(70, static_cast<int>(all.size()));
    for (const LotLayers::Layer* l : all) {
        w.pair(0, "LAYER");
        w.pair(2, l->name);
        w.integer(70, l->locked ? 4 : 0);
        // 꺼진 층은 색 번호를 음수로 적는다 (DXF 관례).
        const int aci = aciFromColor(l->color);
        w.integer(62, l->visible ? aci : -aci);
        w.pair(6, lot_linetype::name(l->linetype));
        ++st.layers;
    }
    w.pair(0, "ENDTAB");
    w.pair(0, "ENDSEC");

    // ---- ENTITIES ----
    w.pair(0, "SECTION");
    w.pair(2, "ENTITIES");
    w.out += body.out;
    w.pair(0, "ENDSEC");
    w.pair(0, "EOF");

    LOT_LOG("dxf: wrote " << st.lines << " lines, " << st.circles << " circles, "
            << st.arcs << " arcs, " << st.polylines << " polylines, "
            << st.texts << " texts, " << st.dimensions << " dimensions (exploded), "
            << st.layers << " layers"
            << (st.skipped ? ", skipped " + std::to_string(st.skipped) + " meshes" : ""));
    if (outStats) *outStats = st;
    return w.out;
}

}  // namespace lot_dxf
