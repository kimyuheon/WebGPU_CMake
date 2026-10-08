// DXF 펼치기 - HATCH (네이티브 lot_dxf_flatten pushHatch 와 같다).
#include "dxf/lot_dxf_emit.h"

#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_dxf {
namespace detail {

// 네이티브 pushHatch 와 같다: 경계 경로 -> 무늬 정의. 실패해도 있는 데까지.
// 맨 바깥 해치는 원점 이동(shiftEntity)에서 빠졌다 - 여기서 해치 통째로 옮긴다.
void Emitter::emitHatch(const Entity& e, const Xform& x, const Entity* parent, int /*depth*/) {
    const std::string& t = e.type;
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
}

}  // namespace detail
}  // namespace lot_dxf
