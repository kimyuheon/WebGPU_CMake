// DXF 공용 도우미: 색 번호 · 선종류 이름 · bulge 호 · NURBS · 글자 코드 · MTEXT 서식 · 헤더 좌표 · 원점 이동.
#include "dxf/lot_dxf_internal.h"

#include "lot_linetype.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace lot_dxf {
namespace detail {
namespace {
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
}  // namespace

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

}  // namespace detail
}  // namespace lot_dxf
