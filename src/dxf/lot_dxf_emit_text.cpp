// DXF 펼치기 - TEXT / ATTRIB / ATTDEF / MTEXT.
#include "dxf/lot_dxf_emit.h"

#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_dxf {
namespace detail {

// ATTRIB = 블록 속성 값. ATTDEF 는 정의라 상수(70 & 2)일 때만 보인다. 70 & 1 = 숨김.
void Emitter::emitText(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    const std::string& t = e.type;
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
}

// 본문은 250 자씩 3 에 나뉘고 마지막이 1 이다
void Emitter::emitMText(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
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
}

}  // namespace detail
}  // namespace lot_dxf
