// DXF 펼치기 - 선 · 원/호 · 폴리선 · 스플라인 · 타원 · 지시선 · SOLID.
#include "dxf/lot_dxf_emit.h"

#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_dxf {
namespace detail {

void Emitter::emitLine(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    addSketch({x.point(vec3{e.num(10), e.num(20), e.num(30)}),
               x.point(vec3{e.num(11), e.num(21), e.num(31)})}, false, e, nullptr);
    ++stats.lines;
}

void Emitter::emitCircle(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    const std::string& t = e.type;
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
}

void Emitter::emitLwPolyline(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
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
}

// NURBS (차수 71, 매듭 40, 제어점 10/20/30, 가중치 41, 맞춤점 11/21/31) - 네이티브와 같다
void Emitter::emitSpline(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
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
}

// 네이티브와 같다: 짧은 축 = (돌출 방향 x 장축) * 비율, 매개변수 41/42 (라디안), 64 분할/바퀴
void Emitter::emitEllipse(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
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
}

// 꼭짓점들을 잇는 열린 폴리선 (화살촉 없음 - 네이티브와 같다)
void Emitter::emitLeader(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    const std::vector<float> lx = e.all(10), ly = e.all(20), lz = e.all(30);
    std::vector<vec3> pts;
    for (size_t i = 0; i < std::min(lx.size(), ly.size()); ++i) pts.push_back(x.point(vec3{lx[i], ly[i], i < lz.size() ? lz[i] : 0.0f}));
    if (pts.size() >= 2) { addSketch(std::move(pts), false, e, nullptr); ++stats.leaders; }
    else noteSkipped("LEADER");
}

// 채운 사각형 - 테두리만 그린다
void Emitter::emitSolid(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    std::vector<vec3> pts{x.point(vec3{e.num(10), e.num(20), e.num(30)}),
                          x.point(vec3{e.num(11), e.num(21), e.num(31)}),
                          x.point(vec3{e.num(13), e.num(23), e.num(33)}),
                          x.point(vec3{e.num(12), e.num(22), e.num(32)})};
    addSketch(std::move(pts), true, e, nullptr);
    ++stats.polylines;
}

}  // namespace detail
}  // namespace lot_dxf
