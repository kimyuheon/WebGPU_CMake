#include "lot_feature_dims.h"

#include "lot_brep_shape.h"
#include "lot_feature.h"
#include "lot_log.h"
#include "lot_model.h"
#include "lot_sketch_tool.h"   // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

// 네이티브 first_app/feature_dims.cpp 의 collectFeatureDims / setFeatureDimensionImpl 을 웹 타입으로 옮긴 것.
// 회전체 · 스윕 (genFeature) 과 기준점 고르기 (앵커) 는 아직 웹에 없다.
namespace lot_feature_dims {
namespace {

using lot::LotBRepShape;
using id_t = LotGameObject::id_t;
using Kind = Dim::Kind;
constexpr unsigned kNone = FeatureLink::kNone;

float len3(const vec3& v) { return std::sqrt(dot(v, v)); }
vec3 unit(const vec3& v) { const float l = len3(v); return l > 1e-12f ? v * (1.0f / l) : v; }
vec3 dirOf(const mat4& m, const vec3& d) { return transformPoint(m, d) - transformPoint(m, vec3{0.0f, 0.0f, 0.0f}); }

vec3 anyPerp(const vec3& n) {
    const vec3 a = std::fabs(n.z) < 0.9f ? vec3{0.0f, 0.0f, 1.0f} : vec3{1.0f, 0.0f, 0.0f};
    return unit(cross(n, a));
}

// 카메라 쪽 · 화면 오른쪽 · 위 (월드) - 치수 앞면과 글자가 읽히는 방향을 고른다
struct View {
    vec3 toCam{0.577f, -0.577f, 0.577f};
    vec3 right{1.0f, 0.0f, 0.0f};
    vec3 up{0.0f, 0.0f, 1.0f};
};

// 정렬 치수 p1 -> p2, 치수선은 바깥쪽 out 으로 off 만큼
Dim aligned(const View& V, Kind k, float value, const std::string& label, vec3 p1, vec3 p2, const vec3& out,
            float off, unsigned sketch = kNone, int axis = -1) {
    Dim d;
    d.kind = k;
    d.value = value;
    d.label = label;
    d.sketch = sketch;
    d.axis = axis;
    d.p1 = p1;
    d.p2 = p2;
    d.dimLine = (p1 + p2) * 0.5f + out * off;
    vec3 n = cross(p2 - p1, out);
    n = len3(n) > 1e-9f ? unit(n) : anyPerp(p2 - p1);
    if (dot(n, V.toCam) < 0.0f) n = n * -1.0f;   // 앞면은 카메라 쪽 - 뒷면에서 보면 거울 글자
    // 진행 방향은 화면 오른쪽 (거의 세로면 화면 위) - 반대면 두 점을 바꿔 글자를 180° 돌린다
    const vec3 dir = unit(p2 - p1);
    const float sx = dot(dir, V.right), sy = dot(dir, V.up);
    if ((std::fabs(sx) >= 0.25f ? sx : sy) < 0.0f) std::swap(d.p1, d.p2);
    d.normal = n;
    return d;
}

// 볼록 사각형 [0,a] x [0,b] 로 다각형 자르기 (서덜랜드-호지먼) - 결과의 범위만 쓴다
bool clipExtents(std::vector<vec2> poly, float a, float b, vec2& lo, vec2& hi) {
    auto clip = [&](auto inside, auto crossAt) {
        std::vector<vec2> out;
        for (size_t i = 0; i < poly.size(); ++i) {
            const vec2 P = poly[i], Q = poly[(i + 1) % poly.size()];
            const bool ip = inside(P), iq = inside(Q);
            if (ip) out.push_back(P);
            if (ip != iq) out.push_back(crossAt(P, Q));
        }
        poly.swap(out);
    };
    auto atX = [](const vec2& P, const vec2& Q, float c) { const float t = (c - P.x) / (Q.x - P.x); return vec2{c, P.y + (Q.y - P.y) * t}; };
    auto atY = [](const vec2& P, const vec2& Q, float c) { const float t = (c - P.y) / (Q.y - P.y); return vec2{P.x + (Q.x - P.x) * t, c}; };
    clip([&](const vec2& p) { return p.x >= 0.0f; }, [&](vec2 P, vec2 Q) { return atX(P, Q, 0.0f); });
    clip([&](const vec2& p) { return p.x <= a; }, [&](vec2 P, vec2 Q) { return atX(P, Q, a); });
    clip([&](const vec2& p) { return p.y >= 0.0f; }, [&](vec2 P, vec2 Q) { return atY(P, Q, 0.0f); });
    clip([&](const vec2& p) { return p.y <= b; }, [&](vec2 P, vec2 Q) { return atY(P, Q, b); });
    if (poly.size() < 3) return false;
    lo = hi = poly.front();
    for (const auto& p : poly) {
        lo = vec2{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y)};
        hi = vec2{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y)};
    }
    return hi.x - lo.x > 1e-6f && hi.y - lo.y > 1e-6f;
}

bool isCircle(const LotGameObject& s) { return s.curve.kind == LotGameObject::Curve::Kind::Circle; }

// 스케치 -> 지금 솔리드 자리: 연결할 때의 월드 -> 로컬 (linkInv) 다음 지금 솔리드 행렬. 옮기지 않았으면 단위 행렬.
mat4 sketchFollow(const LotGameObject& o, unsigned sid) {
    if (sid == kNone || !o.featureLink) return mat4::identity();
    const FeatureLink& L = *o.featureLink;
    const mat4 A = o.transform.mat4Transform();
    if (L.sketch == sid) return A * L.linkInv;
    for (size_t i = 0; i < L.cutSketches.size(); ++i)
        if (L.cutSketches[i] == sid) return A * (i < L.cutLinkInv.size() ? L.cutLinkInv[i] : L.linkInv);
    for (size_t i = 0; i < L.bossSketches.size(); ++i)
        if (L.bossSketches[i] == sid) return A * (i < L.bossLinkInv.size() ? L.bossLinkInv[i] : L.linkInv);
    return mat4::identity();
}

// 일반 닫힌 폴리선 (ㄱ · ㄷ자, 다각형) - 변마다 길이. 12 꼭짓점까지.
void polygonDims(const View& V, const LotGameObject& s, unsigned sid, const std::string& who, const vec3& lift,
                 const vec3& upW, float off, std::vector<Dim>& out, const mat4& follow) {
    const size_t n = s.points.size();
    if (n < 3 || n > 12 || s.hasCurve()) return;
    const mat4 M = follow * s.transform.mat4Transform();
    std::vector<vec3> q(n);
    vec3 ctr{0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < n; ++i) { q[i] = transformPoint(M, s.points[i]) + lift; ctr = ctr + q[i]; }
    ctr = ctr * (1.0f / static_cast<float>(n));
    // 감긴 방향 (단면 평면 법선 기준) - 변의 진짜 바깥 (빈 공간 쪽)
    float wind = 0.0f;
    for (size_t i = 0; i < n; ++i) wind += dot(cross(q[i] - ctr, q[(i + 1) % n] - ctr), upW);
    for (size_t i = 0; i < n; ++i) {
        const vec3 a = q[i], b = q[(i + 1) % n];
        const float l = len3(b - a);
        if (l < 1e-6f) continue;
        vec3 o = unit(cross((b - a) * (1.0f / l), upW));
        if (wind < 0.0f) o = o * -1.0f;
        out.push_back(aligned(V, Kind::Edge, l, who + " 변 " + std::to_string(i + 1), a, b, o, off, sid, static_cast<int>(i)));
    }
}

// 스케치 하나 (원 또는 사각형 · 다각형 닫힌 폴리선) 의 치수. lift = 그 스케치가 만든 면 높이로 올리는 월드 벡터.
// clipW = 컷 스케치면 솔리드 바닥 단면 - 사각형이 그 밖 (허공) 으로 나간 만큼은 뺀다 (열린 키 홈).
void sketchDims(const View& V, const LotGameObject::Map& objs, unsigned sid, const std::string& who, const vec3& lift,
                const vec3& upW, float off, std::vector<Dim>& out, const std::vector<vec3>* clipW, const mat4& follow) {
    const LotGameObject* s = LotGameObject::find(objs, sid);
    if (!s || !s->isSketch()) return;
    const mat4 M = follow * s->transform.mat4Transform();
    if (isCircle(*s)) {
        const vec3 c = transformPoint(M, s->curve.center) + lift;
        const vec3 rv = dirOf(M, s->curve.right * s->curve.radius);
        const float r = len3(rv);
        if (r < 1e-9f) return;
        const vec3 rd = rv * (1.0f / r);
        const vec3 diag = unit(rd + unit(cross(upW, rd)));
        Dim d;
        d.kind = Kind::Diameter;
        d.value = 2.0f * r;
        d.label = who + " Ø";
        d.sketch = sid;
        d.diameter = true;
        d.p1 = c;
        d.p2 = c + diag * r;
        d.dimLine = c + diag * (r + off);
        d.normal = dot(upW, V.toCam) >= 0.0f ? upW : upW * -1.0f;
        out.push_back(d);
        return;
    }
    if (!s->closed) return;
    if (s->points.size() == 4) {
        vec3 q[4];
        for (int i = 0; i < 4; ++i) q[i] = transformPoint(M, s->points[static_cast<size_t>(i)]) + lift;
        const vec3 e0 = q[1] - q[0], e1 = q[2] - q[1];
        const float a = len3(e0), b = len3(e1);
        if (a < 1e-9f || b < 1e-9f) return;
        // 직사각형이면 가로 · 세로 둘 - 아니면 일반 다각형 (변마다)
        if (std::fabs(dot(e0 * (1.0f / a), e1 * (1.0f / b))) > 1e-3f || len3((q[2] - q[3]) - e0) > 1e-3f * a) {
            polygonDims(V, *s, sid, who, lift, upW, off, out, follow);
            return;
        }
        const vec3 ctr = (q[0] + q[1] + q[2] + q[3]) * 0.25f;
        auto outDir = [&](const vec3& a0, const vec3& b0) {
            const vec3 m = (a0 + b0) * 0.5f - ctr;
            return len3(m) > 1e-9f ? unit(m) : anyPerp(b0 - a0);
        };
        const vec3 uh = e0 * (1.0f / a), wh = e1 * (1.0f / b);
        vec2 lo{0.0f, 0.0f}, hi{a, b};
        if (clipW && clipW->size() >= 3) {   // 솔리드에 닿는 범위만
            std::vector<vec2> poly;
            for (const vec3& p : *clipW) poly.push_back(vec2{dot(p - q[0], uh), dot(p - q[0], wh)});
            vec2 l2, h2;
            if (clipExtents(poly, a, b, l2, h2)) { lo = l2; hi = h2; }
        }
        const float tolA = 1e-4f * a, tolB = 1e-4f * b;
        auto side = [](bool nearCut, bool farCut) { return farCut && !nearCut ? 1 : (nearCut && !farCut ? 2 : 0); };
        Dim w = aligned(V, Kind::Width, hi.x - lo.x, who + " 가로", q[0] + uh * lo.x, q[0] + uh * hi.x, outDir(q[0], q[1]), off, sid);
        w.clippedSide = side(lo.x > tolA, hi.x < a - tolA);
        out.push_back(w);
        Dim l = aligned(V, Kind::Length, hi.y - lo.y, who + " 세로", q[1] + wh * lo.y, q[1] + wh * hi.y, outDir(q[1], q[2]), off, sid);
        l.clippedSide = side(lo.y > tolB, hi.y < b - tolB);
        out.push_back(l);
        return;
    }
    polygonDims(V, *s, sid, who, lift, upW, off, out, follow);
}

// 위치 치수의 기준 모서리 (원 스케치 id -> 바닥 단면의 모서리 번호 둘) - 한 번 정하면 그대로 (네이티브 gLocEdges)
struct LocEdges { int outline = 0, a = -1, b = -1; };
std::unordered_map<unsigned, LocEdges>& locEdges() {
    static std::unordered_map<unsigned, LocEdges> m;
    return m;
}

// 같은 중심 지름 (보스 Ø44 안 관통 Ø24) - 지시선이 같은 방향이면 글이 겹친다. k 번째는 법선 둘레로 k x 90° 돌린다.
void spreadConcentricDiameters(const View& V, std::vector<Dim>& out, float size) {
    const float eps = 1e-3f * std::fmax(1e-3f, size);
    auto sameCenter = [&](const Dim& a, const Dim& b) {
        if (std::fabs(dot(unit(a.normal), unit(b.normal))) < 0.999f) return false;
        const vec3 d = a.p1 - b.p1;
        return len3(d - unit(a.normal) * dot(d, unit(a.normal))) < eps;   // 축 방향 (높이) 차이는 보지 않는다
    };
    const std::vector<Dim> orig = out;
    for (size_t i = 0; i < out.size(); ++i) {
        if (!orig[i].diameter) continue;
        int k = 0, group = 0;
        float rOut = 0.0f;
        for (size_t j = 0; j < orig.size(); ++j) {
            if (!orig[j].diameter || !sameCenter(orig[i], orig[j])) continue;
            ++group;
            if (j < i) ++k;
            rOut = std::fmax(rOut, len3(orig[j].p2 - orig[j].p1));
        }
        if (group < 2) continue;
        Dim& d = out[i];
        const float r = len3(d.p2 - d.p1);
        if (r < 1e-9f) continue;
        const float gap = std::fmax(0.0f, len3(d.dimLine - d.p1) - r);
        const vec3 n = unit(d.normal);
        const vec3 base = (d.p2 - d.p1) * (1.0f / r);
        const float ang = 1.57079633f * static_cast<float>(k);
        const vec3 a = rotate(quat::angleAxis(ang, n), base), b = rotate(quat::angleAxis(-ang, n), base);
        const vec3 tc = V.toCam - n * dot(V.toCam, n);   // 돌리는 쪽 = 카메라 쪽 (뒤로 돌리면 보스 벽에 가린다)
        const vec3 dir = (len3(tc) > 1e-3f && dot(b, tc) > dot(a, tc) + 1e-4f) ? b : a;
        d.p2 = d.p1 + dir * r;
        d.dimLine = d.p1 + dir * (rOut + gap);
    }
}

float worldSize(const LotGameObject& o) {
    if (!o.model) return 1.0f;
    const mat4 m = o.transform.mat4Transform();
    const vec3& a = o.model->boundsMin();
    const vec3& b = o.model->boundsMax();
    vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < 8; ++i) {
        const vec3 p = transformPoint(m, vec3{(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z});
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    }
    return std::fmax(1e-3f, len3(hi - lo));
}

// 행렬의 선형 부분의 역으로 방향 하나를 되돌린다 (솔리드를 따라간 자리 -> 스케치 좌표)
vec3 inverseLinear(const mat4& m, const vec3& v) {
    const vec3 c0{m.m[0][0], m.m[0][1], m.m[0][2]}, c1{m.m[1][0], m.m[1][1], m.m[1][2]}, c2{m.m[2][0], m.m[2][1], m.m[2][2]};
    const float det = dot(c0, cross(c1, c2));
    if (std::fabs(det) < 1e-20f) return v;
    // M^-1 v = (v . (c1 x c2), v . (c2 x c0), v . (c0 x c1)) / det
    return vec3{dot(v, cross(c1, c2)), dot(v, cross(c2, c0)), dot(v, cross(c0, c1))} * (1.0f / det);
}

}  // namespace

std::vector<Dim> collect(const LotGameObject::Map& objs, id_t id, const vec3& toCamIn) {
    View V;
    V.toCam = len3(toCamIn) > 1e-6f ? unit(toCamIn) : vec3{0.0f, 0.0f, 1.0f};
    {
        const vec3 f = V.toCam * -1.0f;
        vec3 r = cross(f, vec3{0.0f, 0.0f, 1.0f});
        if (len3(r) < 1e-4f) r = cross(f, vec3{0.0f, 1.0f, 0.0f});   // 위에서 똑바로 볼 때
        V.right = unit(r);
        V.up = unit(cross(V.right, f));
    }
    std::vector<Dim> out;
    const LotGameObject* op = LotGameObject::find(objs, id);
    if (!op || !op->brep) return out;
    const LotGameObject& o = *op;
    const auto& F = o.brep->feature();
    const mat4 M = o.transform.mat4Transform();
    auto W = [&](const vec3& v) { return transformPoint(M, v); };
    const float size = worldSize(o);
    const float off = 0.08f * size;

    if (F.kind == LotBRepShape::FeatureKind::Box) {
        const vec3 h = F.dimensions * 0.5f, c = F.origin;
        const vec3 xw = unit(dirOf(M, vec3{1.0f, 0.0f, 0.0f})), yw = unit(dirOf(M, vec3{0.0f, 1.0f, 0.0f}));
        const vec3 a = W(c + vec3{-h.x, -h.y, -h.z}), bx = W(c + vec3{h.x, -h.y, -h.z});
        const vec3 by = W(c + vec3{h.x, h.y, -h.z}), bz = W(c + vec3{h.x, -h.y, h.z});
        out.push_back(aligned(V, Kind::Width, len3(bx - a), "가로", a, bx, yw * -1.0f, off, kNone, 0));
        out.push_back(aligned(V, Kind::Length, len3(by - bx), "세로", bx, by, xw, off, kNone, 1));
        out.push_back(aligned(V, Kind::Height, len3(bz - bx), "높이", bx, bz, unit(xw - yw), off, kNone, 2));
        return out;
    }
    if (F.kind == LotBRepShape::FeatureKind::Cylinder) {
        const vec3 dW = unit(dirOf(M, F.direction));
        const vec3 lo = W(F.origin - F.direction * (F.height * 0.5f)), hi = W(F.origin + F.direction * (F.height * 0.5f));
        const vec3 rd = anyPerp(dW);
        const float r = len3(dirOf(M, anyPerp(F.direction) * F.radius));
        Dim d;
        d.kind = Kind::Diameter;
        d.value = 2.0f * r;
        d.label = "지름";
        d.diameter = true;
        d.p1 = hi;
        d.p2 = hi + rd * r;
        d.dimLine = hi + rd * (r + off);
        d.normal = dot(dW, V.toCam) >= 0.0f ? dW : dW * -1.0f;
        out.push_back(d);
        out.push_back(aligned(V, Kind::Height, len3(hi - lo), "높이", lo + rd * r, hi + rd * r, rd, off));
        return out;
    }
    if (F.kind != LotBRepShape::FeatureKind::Extrude || F.profile.size() < 3) return out;

    // 돌출 높이 - 단면 중심에서 가장 먼 꼭짓점의 세로 모서리, 바깥으로
    const vec3 dirL = unit(F.direction);
    const vec3 topL = dirL * F.height;
    vec3 cL{0.0f, 0.0f, 0.0f};
    for (const vec3& p : F.profile) cL = cL + p;
    cL = cL * (1.0f / static_cast<float>(F.profile.size()));
    size_t far = 0;
    for (size_t i = 1; i < F.profile.size(); ++i)
        if (len3(F.profile[i] - cL) > len3(F.profile[far] - cL)) far = i;
    const vec3 b0 = W(F.profile[far]), t0 = W(F.profile[far] + topL);
    const vec3 upW = unit(t0 - b0);
    // 바깥 방향 후보 중 치수 평면 (세로) 이 카메라를 가장 정면으로 보는 것
    const size_t n = F.profile.size();
    const vec3 cands[3] = {b0 - W(cL), b0 - W(F.profile[(far + n - 1) % n]), b0 - W(F.profile[(far + 1) % n])};
    vec3 outW = anyPerp(upW);
    float best = -1.0f;
    for (vec3 c : cands) {
        c = c - upW * dot(c, upW);
        if (len3(c) < 1e-9f) continue;
        c = unit(c);
        const float face = std::fabs(dot(unit(cross(upW, c)), V.toCam));
        if (face > best + 1e-3f) { best = face; outW = c; }
    }
    out.push_back(aligned(V, Kind::Height, len3(t0 - b0), "돌출 높이", b0, t0, outW, off));

    // 보스 높이 - 화면 오른쪽 윤곽의 꼭짓점 세로 모서리 (붙는 캡 -> 끝 캡). axis = 100 + 보스 번호.
    const float hW = len3(t0 - b0);
    const float wpl = F.height > 1e-9f ? hW / F.height : 1.0f;   // 높이 방향 월드 / 로컬
    for (size_t k = 0; k < F.bosses.size(); ++k) {
        const auto& B = F.bosses[k];
        if (B.profile.size() < 3) continue;
        vec3 bc{0.0f, 0.0f, 0.0f};
        for (const vec3& p : B.profile) bc = bc + p;
        bc = bc * (1.0f / static_cast<float>(B.profile.size()));
        vec3 rightW = V.right - upW * dot(V.right, upW);
        rightW = len3(rightW) < 1e-6f ? anyPerp(upW) : unit(rightW);
        size_t bf = 0;
        float bestR = -1e30f;
        for (size_t i = 0; i < B.profile.size(); ++i) {
            const float r = dot(W(B.profile[i]) - W(bc), rightW);
            if (r > bestR + 1e-6f) { bestR = r; bf = i; }
        }
        const float nearL = B.height > 0.0f ? F.height : 0.0f, farL = B.height > 0.0f ? F.height + B.height : B.height;
        const vec3 a = W(B.profile[bf] + dirL * nearL), bT = W(B.profile[bf] + dirL * farL);
        vec3 ob = W(B.profile[bf]) - W(bc);
        ob = ob - upW * dot(ob, upW);
        ob = len3(ob) > 1e-9f ? unit(ob) : anyPerp(upW);
        out.push_back(aligned(V, Kind::Height, len3(bT - a), "보스 " + std::to_string(k + 1) + " 높이", a, bT, ob, off * 0.6f,
                              kNone, static_cast<int>(100 + k)));
    }

    // 스케치 치수 - 그 스케치가 만든 면 높이로 올려서 (바닥 스케치는 솔리드에 가려진다), 면에서 살짝 더 띄운다.
    const vec3 baseW = W(F.profile[0]);
    auto liftFor = [&](unsigned sid, float level) {
        const LotGameObject* st = LotGameObject::find(objs, sid);
        if (!st) return vec3{0.0f, 0.0f, 0.0f};
        const mat4 SM = sketchFollow(o, sid) * st->transform.mat4Transform();
        const vec3 anchor = isCircle(*st) ? transformPoint(SM, st->curve.center)
                          : (!st->points.empty() ? transformPoint(SM, st->points[0]) : transformPoint(SM, vec3{0.0f, 0.0f, 0.0f}));
        const float lvW = level * wpl;
        return upW * (lvW - dot(anchor - baseW, upW) + (level < 0.0f ? -0.002f : 0.002f) * size);
    };
    std::vector<vec3> outlineW;   // 바닥 단면 (월드) - 컷 치수를 솔리드에 닿는 부분으로 자른다
    for (const vec3& p : F.profile) outlineW.push_back(W(p));
    if (o.featureLink) {
        const FeatureLink& L = *o.featureLink;
        if (L.sketch != kNone)
            sketchDims(V, objs, L.sketch, "단면", liftFor(L.sketch, F.height), upW, off, out, nullptr, sketchFollow(o, L.sketch));
        for (size_t i = 0; i < L.bossSketches.size() && i < F.bosses.size(); ++i) {
            if (L.bossSketches[i] == kNone) continue;
            const float h = F.bosses[i].height;
            sketchDims(V, objs, L.bossSketches[i], "보스 " + std::to_string(i + 1), liftFor(L.bossSketches[i], h > 0.0f ? F.height + h : h),
                       upW, off * 0.8f, out, nullptr, sketchFollow(o, L.bossSketches[i]));
        }
        for (size_t i = 0; i < L.cutSketches.size(); ++i) {
            if (L.cutSketches[i] == kNone) continue;
            float top = F.height, bottom = 0.0f;
            o.brep->cutSpan(i, top, bottom);   // 위 보스 안 컷은 보스 끝에서 보인다
            sketchDims(V, objs, L.cutSketches[i], "컷 " + std::to_string(i + 1), liftFor(L.cutSketches[i], top), upW, off * 0.6f, out,
                       &outlineW, sketchFollow(o, L.cutSketches[i]));
        }

        // 위치 치수 - 원 컷 · 원 보스 중심에서 바닥 단면의 가장 가까운 모서리와, 그와 나란하지 않은 가장 가까운 모서리까지.
        struct Src { unsigned sid; std::string who; float level; };
        std::vector<Src> srcs;
        for (size_t i = 0; i < L.cutSketches.size(); ++i) {
            if (L.cutSketches[i] == kNone) continue;
            float top = F.height, bottom = 0.0f;
            o.brep->cutSpan(i, top, bottom);
            srcs.push_back({L.cutSketches[i], "컷 " + std::to_string(i + 1), top});
        }
        for (size_t i = 0; i < L.bossSketches.size() && i < F.bosses.size(); ++i)
            if (L.bossSketches[i] != kNone)
                srcs.push_back({L.bossSketches[i], "보스 " + std::to_string(i + 1),
                                F.bosses[i].height > 0.0f ? F.height + F.bosses[i].height : F.bosses[i].height});
        vec3 oc{0.0f, 0.0f, 0.0f};
        for (const vec3& p : outlineW) oc = oc + p;
        oc = oc * (1.0f / static_cast<float>(std::max<size_t>(1, outlineW.size())));
        auto planar = [&](const vec3& v) { return v - upW * dot(v, upW); };
        std::vector<vec3> located;
        int k = 0;
        for (const Src& src : srcs) {
            const LotGameObject* st = LotGameObject::find(objs, src.sid);
            if (!st || !isCircle(*st)) continue;
            const mat4 SM = sketchFollow(o, src.sid) * st->transform.mat4Transform();
            const vec3 c = transformPoint(SM, st->curve.center) + liftFor(src.sid, src.level);
            const float r = len3(dirOf(SM, st->curve.right * st->curve.radius));
            if (std::any_of(located.begin(), located.end(), [&](const vec3& q) { return len3(planar(q - c)) < 1e-3f * size; })) continue;
            located.push_back(c);
            struct Hit { float d; vec3 foot, e; int edge; bool inRange; };
            std::vector<Hit> hits;
            for (size_t i = 0; i < outlineW.size(); ++i) {
                const vec3 a = outlineW[i], b = outlineW[(i + 1) % outlineW.size()];
                vec3 e = planar(b - a);
                const float le = len3(e);
                if (le < 1e-6f) continue;
                e = e * (1.0f / le);
                const vec3 v = planar(c - a);
                const float t = dot(v, e);
                const bool inRange = t >= -1e-3f * le && t <= le * (1.0f + 1e-3f);
                const vec3 foot = c - (v - e * t);
                hits.push_back({len3(v - e * t), foot, e, static_cast<int>(i), inRange});
            }
            // 기준 모서리는 한 번 정하면 그대로 - 매번 가장 가까운 것을 고르면 구멍을 옮기는 순간 반대쪽으로 바뀐다
            std::vector<Hit> pick;
            auto mem = locEdges().find(src.sid);
            if (mem != locEdges().end() && mem->second.outline == static_cast<int>(outlineW.size())) {
                for (int ei : {mem->second.a, mem->second.b})
                    for (const auto& h : hits) if (h.edge == ei) pick.push_back(h);
            }
            if (pick.empty()) {
                std::vector<Hit> in;
                for (const auto& h : hits) if (h.inRange) in.push_back(h);
                if (in.empty()) continue;
                std::sort(in.begin(), in.end(), [](const Hit& x, const Hit& y) {
                    return x.d < y.d - 1e-4f || (std::fabs(x.d - y.d) <= 1e-4f && x.edge < y.edge);
                });
                pick.push_back(in.front());
                for (const auto& h : in) if (std::fabs(dot(h.e, pick.front().e)) < 0.5f) { pick.push_back(h); break; }
                locEdges()[src.sid] = {static_cast<int>(outlineW.size()), pick[0].edge, pick.size() > 1 ? pick[1].edge : -1};
            }
            for (size_t m = 0; m < pick.size(); ++m) {
                const Hit& h = pick[m];
                if (h.d < 1e-4f) continue;
                vec3 sideV = h.e;   // 치수선은 모서리 방향으로 비켜 - 판 안쪽
                if (dot(h.foot - oc, sideV) > 0.0f) sideV = sideV * -1.0f;
                Dim d = aligned(V, Kind::Location, h.d, src.who + " 위치 " + std::to_string(m + 1), h.foot, c, sideV,
                                r + 0.03f * size, src.sid, 300 + k);
                d.locDir = unit(c - h.foot);
                out.push_back(d);
            }
            ++k;
        }
    }
    spreadConcentricDiameters(V, out, size);
    return out;
}

bool set(LotGameObject::Map& objects, id_t id, int index, float value, lot_web_device& device, EditHistory& history,
         std::string& why) {
    if (!(value > 0.0f) || !std::isfinite(value)) { why = "type a positive value"; return false; }
    LotGameObject* solid = LotGameObject::find(objects, id);
    if (!solid || !solid->brep) { why = "not a solid"; return false; }
    // 지금 보이는 대로 같은 번호를 다시 모은다 (방향은 값에 영향이 없다)
    const auto dims = collect(objects, id, vec3{0.577f, -0.577f, 0.577f});
    if (index < 0 || index >= static_cast<int>(dims.size())) { why = "no such dimension"; return false; }
    const Dim& d = dims[static_cast<size_t>(index)];
    const float ratio = value / std::fmax(1e-9f, d.value);   // 월드 -> 로컬: 지금 값과의 비율로 (축척이 있어도 맞다)
    const auto& F = solid->brep->feature();

    if (d.sketch == kNone) {   // 솔리드 매개변수 (B-Rep)
        if (F.kind == LotBRepShape::FeatureKind::Extrude) {
            if (d.axis >= 100) {
                const size_t k = static_cast<size_t>(d.axis - 100);
                return k < F.bosses.size() && lot_feature::setBossHeight(objects, id, static_cast<unsigned>(k), F.bosses[k].height * ratio,
                                                                         device, history, why);
            }
            return lot_feature::setHeight(objects, id, F.height * ratio, device, history, why);
        }
        why = "box / cylinder dimensions are not editable on the web yet";
        return false;
    }

    // 스케치 치수 - 스케치를 고치고 그 자리에서 다시 만들어 본다 (한 번의 실행 취소: 솔리드 + 스케치)
    LotGameObject* s = LotGameObject::find(objects, d.sketch);
    if (!s) { why = "the sketch is gone"; return false; }
    const std::string errBefore = solid->featureLink ? solid->featureLink->error : std::string();
    EditHistory::Edit edit;
    edit.label = "feature dimension";
    edit.before = {EditHistory::Record::capture(*solid), EditHistory::Record::capture(*s)};

    bool ok = true;
    if (d.kind == Kind::Location) {   // 원 스케치를 가장자리에서 멀어지게 (값이 커지면) 민다 - 구멍은 재생성이 따라온다
        const vec3 sv = d.locDir * (value - d.value);
        s->transform.translation = s->transform.translation + inverseLinear(sketchFollow(*solid, d.sketch), sv);
    } else if (d.kind == Kind::Diameter && isCircle(*s)) {
        s->curve.radius *= ratio;
        s->points = tessellateArc(s->curve.center, s->curve.radius, s->curve.right, s->curve.up, 0.0f, 6.28318530718f, false);
    } else {
        std::vector<vec3> q = s->worldPoints();
        const size_t n = q.size();
        if (d.kind == Kind::Edge) {
            // 변 하나 - STRETCH: 변 방향으로 변 끝 (b) 이상 나가 있는 꼭짓점을 전부 같은 만큼 민다 (ㄱ · ㄷ자 직각 유지)
            const size_t i = static_cast<size_t>(d.axis);
            if (d.axis < 0 || i >= n) { ok = false; }
            else {
                const vec3 a = q[i], b = q[(i + 1) % n];
                const float l = len3(b - a);
                if (l < 1e-6f) ok = false;
                else {
                    const vec3 dir = (b - a) * (1.0f / l);
                    const vec3 delta = dir * (value - l);
                    for (vec3& p : q) if (dot(p - a, dir) >= l - 1e-4f * l) p = p + delta;
                }
            }
        } else if (n == 4) {
            // 직사각형 - 첫 꼭짓점 고정, 고친 변 방향으로만. 보이는 값 (솔리드에 닿는 부분) 만큼 - 먼 변이 허공이면 가까운 변을 민다
            const vec3 u = unit(q[1] - q[0]), w = unit(q[2] - q[1]);
            float a = len3(q[1] - q[0]), b = len3(q[2] - q[1]);
            const float delta = value - d.value;
            vec3 o0 = q[0];
            if (d.kind == Kind::Width) { a += delta; if (d.clippedSide == 1) o0 = o0 - u * delta; }
            else { b += delta; if (d.clippedSide == 1) o0 = o0 - w * delta; }
            if (!(a > 1e-6f) || !(b > 1e-6f)) ok = false;
            else { q[0] = o0; q[1] = q[0] + u * a; q[2] = q[1] + w * b; q[3] = q[0] + w * b; }
        } else {
            ok = false;
        }
        if (ok) for (size_t i = 0; i < n; ++i) s->points[i] = s->transform.worldToLocalPoint(q[i]);
    }
    if (!ok) {
        for (const auto& r : edit.before) if (LotGameObject* t = LotGameObject::find(objects, r.id)) r.apply(*t);
        why = "cannot change that dimension";
        return false;
    }
    lot_feature::regenerate(objects, device);
    const std::string errAfter = solid->featureLink ? solid->featureLink->error : std::string();
    if (!errAfter.empty() && errAfter != errBefore) {   // 새 실패 (구멍이 판 밖 허공으로 등) - 되돌린다
        for (const auto& r : edit.before) if (LotGameObject* t = LotGameObject::find(objects, r.id)) r.apply(*t);
        lot_feature::regenerate(objects, device);
        why = errAfter;
        return false;
    }
    edit.after = {EditHistory::Record::capture(*solid), EditHistory::Record::capture(*s)};
    history.record(std::move(edit));
    LOT_LOG("feature: dimension " << d.label << " " << d.value << " -> " << value << " (solid " << id << ")");
    return true;
}

}  // namespace lot_feature_dims
