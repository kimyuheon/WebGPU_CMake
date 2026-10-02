#include "lot_cursor_snap.h"

#include <cmath>
#include <cstdio>

namespace lot_cursor {
namespace {

Settings g_settings;

// 평면 축 하나에 대한 성분을 눈금에 맞춘다
float snapTo(float value, float step) {
    return (step > 0.0f) ? std::round(value / step) * step : value;
}

}  // namespace

Settings& settings() { return g_settings; }

std::string statusSuffix() {
    std::string s;
    if (g_settings.ortho) s += "  ORTHO";
    else if (g_settings.polar) s += "  POLAR";
    if (g_settings.gridSpacing > 0.0f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "  SNAP %g", g_settings.gridSpacing);
        s += buf;
    }
    return s;
}

float niceSpacing(float target) {
    if (!(target > 0.0f)) return 0.5f;
    const float exp10 = std::floor(std::log10(target));
    const float base = std::pow(10.0f, exp10);
    const float mant = target / base;           // [1, 10)
    const float choice = (mant < 1.5f) ? 1.0f : (mant < 3.5f) ? 2.0f : (mant < 7.5f) ? 5.0f : 10.0f;
    return choice * base;
}

vec3 apply(const vec3& point, const SketchPlane& plane, const vec3* reference) {
    vec3 p = point;

    // 직교 트랙킹: 기준점에서 평면의 두 축 중 더 많이 움직인 쪽으로만 나간다.
    // 축은 매 프레임 다시 고르므로 드래그하다 방향을 틀면 자연스럽게 바뀐다.
    if (g_settings.ortho && reference) {
        const vec3 d = p - *reference;
        const float dr = dot(d, plane.right), du = dot(d, plane.up);
        p = (std::fabs(dr) >= std::fabs(du)) ? *reference + plane.right * dr
                                             : *reference + plane.up * du;
    } else if (g_settings.polar && reference && g_settings.polarStepDeg > 0.0f) {
        // 극좌표 트랙킹: 직교와 달리 늘 붙지 않고, 배수 각도에서 kPolarCatchDeg 안일 때만
        // 그 방향 선 위로 내린다 (AutoCAD 와 같다 - 멀면 커서를 그대로 둔다).
        constexpr float kPolarCatchDeg = 4.0f;
        constexpr float kDegPerRad = 57.2957795f;
        const vec3 d = p - *reference;
        const float dr = dot(d, plane.right), du = dot(d, plane.up);
        const float len = std::sqrt(dr * dr + du * du);
        if (len > 0.0f) {
            const float deg = std::atan2(du, dr) * kDegPerRad;
            const float snapped = std::round(deg / g_settings.polarStepDeg) * g_settings.polarStepDeg;
            if (std::fabs(deg - snapped) <= kPolarCatchDeg) {
                const float a = snapped / kDegPerRad;
                const vec3 dir = plane.right * std::cos(a) + plane.up * std::sin(a);
                p = *reference + dir * (dr * std::cos(a) + du * std::sin(a));
            }
        }
    }

    // 그리드 스냅: 평면 좌표계에서 눈금에 맞춘다. 직교 트랙킹 뒤에 하므로
    // 둘 다 켜면 '축 방향으로 눈금만큼' 이 된다.
    if (g_settings.gridSpacing > 0.0f) {
        const vec3 rel = p - plane.origin;
        const float r = snapTo(dot(rel, plane.right), g_settings.gridSpacing);
        const float u = snapTo(dot(rel, plane.up), g_settings.gridSpacing);
        const float n = dot(rel, plane.normal);  // 평면 밖 성분은 그대로 둔다
        p = plane.origin + plane.right * r + plane.up * u + plane.normal * n;
    }
    return p;
}

}  // namespace lot_cursor
