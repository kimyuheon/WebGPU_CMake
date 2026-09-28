#include "lot_grid.h"
#include "line_render_system.h"
#include "lot_camera.h"

#include <cmath>

namespace lot_grid {
namespace {

const vec3 kAxisXColor{0.8f, 0.25f, 0.25f};   // X 축 - 붉은색 (CAD 관례)
const vec3 kAxisYColor{0.25f, 0.8f, 0.3f};    // Y 축 - 초록색
const vec3 kMajorColor{0.38f, 0.38f, 0.42f};  // 다섯 칸마다
const vec3 kMinorColor{0.22f, 0.22f, 0.25f};

constexpr int kMajorEvery = 5;
constexpr int kMaxLines = 400;          // 한 방향 선 수 상한 (줌 아웃 방어)
constexpr float kMinMinorPx = 6.0f;     // 눈금선이 이보다 촘촘하면 굵은 선만 그린다

}  // namespace

void draw(LineRenderSystem& lines, const LotCamera& camera, float spacing, float viewportHeight) {
    if (!(spacing > 0.0f)) return;

    // 화면에 담길 범위. 바닥 한가운데(타깃을 z=0 에 내린 점) 둘레로 그린다.
    const vec3 target = camera.getTarget();
    const vec3 center{target.x, target.y, 0.0f};
    const float worldPerPixel = camera.worldPerPixel(center, viewportHeight);
    float half = worldPerPixel * viewportHeight;  // 세로 한 화면 = 그 정도, 가로까지 덮게 두 배쯤
    if (!(half > 0.0f)) return;

    // 촘촘하면 눈금선을 건너뛰고 굵은 선만 (줌 아웃했을 때 회색 덩어리가 되지 않게)
    const bool minorVisible = (spacing / worldPerPixel) >= kMinMinorPx;
    const float step = minorVisible ? spacing : spacing * kMajorEvery;

    int count = static_cast<int>(half / step) + 2;
    if (count > kMaxLines) {
        count = kMaxLines;
        half = count * step;
    }

    // 눈금은 월드 원점 기준이라 카메라가 움직여도 선이 미끄러지지 않는다
    const float baseX = std::round(center.x / step) * step;
    const float baseY = std::round(center.y / step) * step;
    const float spanX = count * step, spanY = count * step;

    auto colorFor = [&](float coord) {
        if (std::fabs(coord) < step * 0.25f) return vec3{0.0f, 0.0f, 0.0f};  // 축은 따로 그린다
        const float k = coord / spacing;
        const bool major = std::fabs(k / kMajorEvery - std::round(k / kMajorEvery)) < 1e-3f;
        return major ? kMajorColor : kMinorColor;
    };

    for (int i = -count; i <= count; ++i) {
        const float x = baseX + i * step;
        const float y = baseY + i * step;
        // Y 방향으로 뻗는 선 (x 고정)
        if (std::fabs(x) >= step * 0.25f) {
            lines.addLine(vec3{x, baseY - spanY, 0.0f}, vec3{x, baseY + spanY, 0.0f}, colorFor(x));
        }
        // X 방향으로 뻗는 선 (y 고정)
        if (std::fabs(y) >= step * 0.25f) {
            lines.addLine(vec3{baseX - spanX, y, 0.0f}, vec3{baseX + spanX, y, 0.0f}, colorFor(y));
        }
    }

    // 축선은 격자가 끝나는 데까지 한 번에
    lines.addLine(vec3{baseX - spanX, 0.0f, 0.0f}, vec3{baseX + spanX, 0.0f, 0.0f}, kAxisXColor);
    lines.addLine(vec3{0.0f, baseY - spanY, 0.0f}, vec3{0.0f, baseY + spanY, 0.0f}, kAxisYColor);
}

}  // namespace lot_grid
