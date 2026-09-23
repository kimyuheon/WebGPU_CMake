#include "lot_linetype.h"
#include "line_render_system.h"

#include <cmath>

namespace lot_linetype {
namespace {

// 네이티브 lot_linetype.h 의 addStandard() 와 같은 id / 무늬여야 한다.
const std::vector<Definition> kStandard = {
    {kContinuous, "Continuous", "__________", {}},
    {1, "Dashed",  "__ __ __ __",   {0.5f, -0.25f}},
    {2, "Hidden",  "_ _ _ _ _ _",   {0.25f, -0.125f}},
    {3, "Center",  "____ _ ____ _", {1.25f, -0.25f, 0.25f, -0.25f}},
    {4, "Phantom", "____ _ _ ____", {1.25f, -0.25f, 0.25f, -0.25f, 0.25f, -0.25f}},
    {5, "Dot",     ". . . . . .",   {0.0f, -0.25f}},
    {6, "DashDot", "__ . __ . __",  {0.5f, -0.25f, 0.0f, -0.25f}},
    {7, "Divide",  "__ . . __ . .", {0.5f, -0.25f, 0.0f, -0.25f, 0.0f, -0.25f}},
    {8, "Border",  "__ __ . __ __", {0.5f, -0.25f, 0.5f, -0.25f, 0.0f, -0.25f}},
};

float length(const vec3& v) { return std::sqrt(dot(v, v)); }

// 점은 길이가 0 이라 그냥 두면 안 보인다 - 아주 짧은 선으로 낸다.
constexpr float kDotFraction = 0.06f;  // 무늬 한 주기의 이 비율만큼

}  // namespace

const std::vector<Definition>& standard() { return kStandard; }

const Definition* find(uint32_t id) {
    for (const Definition& d : kStandard) {
        if (d.id == id && !d.pattern.empty()) return &d;
    }
    return nullptr;
}

const char* name(uint32_t id) {
    if (id == kByLayer) return "ByLayer";
    for (const Definition& d : kStandard) {
        if (d.id == id) return d.name;
    }
    return "Continuous";
}

void emit(LineRenderSystem& lines, const std::vector<vec3>& points, bool closed,
          const vec3& color, uint32_t linetype, float scale, float minDashWorld) {
    const size_t n = points.size();
    if (n < 2) return;
    const size_t segments = closed ? n : n - 1;

    const Definition* def = find(linetype);
    float cycle = 0.0f;
    if (def) {
        for (float e : def->pattern) cycle += std::fabs(e);
        cycle *= (scale > 0.0f) ? scale : 1.0f;
    }
    // 무늬가 없거나, 화면에서 무늬 한 주기가 몇 픽셀도 안 되면 실선으로 (점선 뭉개짐 방지)
    if (!def || cycle <= 1e-6f || cycle < minDashWorld) {
        for (size_t i = 0; i < segments; ++i) lines.addLine(points[i], points[(i + 1) % n], color);
        return;
    }

    // 폴리라인 전체를 하나의 누적 길이로 본다 - 꼭짓점에서 무늬가 끊기지 않게
    // (AutoCAD 도 엔티티 시작부터 누적한다).
    const float ltScale = (scale > 0.0f) ? scale : 1.0f;
    size_t element = 0;                                   // 지금 무늬 원소
    float remaining = std::fabs(def->pattern[0]) * ltScale;  // 그 원소에 남은 길이
    bool ink = def->pattern[0] >= 0.0f;                   // 선인가 (점도 선으로 본다)
    bool isDot = def->pattern[0] == 0.0f;
    const float dotLen = cycle * kDotFraction;

    auto advance = [&]() {
        element = (element + 1) % def->pattern.size();
        const float e = def->pattern[element];
        ink = e >= 0.0f;
        isDot = e == 0.0f;
        remaining = (isDot ? dotLen : std::fabs(e) * ltScale);
    };
    if (isDot) remaining = dotLen;

    for (size_t i = 0; i < segments; ++i) {
        const vec3& a = points[i];
        const vec3& b = points[(i + 1) % n];
        const float segLen = length(b - a);
        if (segLen < 1e-9f) continue;
        const vec3 dir = (b - a) * (1.0f / segLen);

        float travelled = 0.0f;
        while (travelled < segLen) {
            const float step = std::fmin(remaining, segLen - travelled);
            if (ink) {
                lines.addLine(a + dir * travelled, a + dir * (travelled + step), color);
            }
            travelled += step;
            remaining -= step;
            if (remaining <= 1e-9f) advance();
        }
    }
}

}  // namespace lot_linetype
