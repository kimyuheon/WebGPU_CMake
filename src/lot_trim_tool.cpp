#include "lot_trim_tool.h"
#include "line_render_system.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "lot_picking.h"
#include "lot_sketch_tool.h"   // SketchPlane, tessellateArc

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {

constexpr float kTwoPi = 6.28318530718f;
const vec3 kRemoveColor{1.0f, 0.35f, 0.35f};
const vec3 kExtendColor{0.4f, 0.8f, 1.0f};

struct V2 {
    float x = 0.0f, y = 0.0f;
};
V2 operator-(V2 a, V2 b) { return V2{a.x - b.x, a.y - b.y}; }
V2 operator+(V2 a, V2 b) { return V2{a.x + b.x, a.y + b.y}; }
V2 operator*(V2 a, float s) { return V2{a.x * s, a.y * s}; }
float dot2(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
float cross2(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
float len3(const vec3& v) { return std::sqrt(dot(v, v)); }
float wrap2pi(float a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0f ? a + kTwoPi : a;
}

// 작업평면에 투영한 한 객체의 모양. 폴리선(선 포함) 또는 원/호.
struct Shape {
    LotGameObject::id_t id = LotGameObject::kInvalidId;
    bool curve = false;
    // 폴리선
    std::vector<vec3> p3;        // 월드 점
    std::vector<V2> p2;          // 투영
    std::vector<float> cum;      // 점까지의 누적 길이 (3D) - 폴리선 위치 매개변수
    bool closed = false;
    // 원 / 호 (월드 축, 각은 그 축 기준 라디안)
    vec3 c3{};
    vec3 rx{}, ry{};
    float r = 0.0f;
    bool isArc = false;
    float a0 = 0.0f, a1 = kTwoPi;
    V2 c2{};
    // 투영 경계상자
    V2 lo{}, hi{};

    size_t segCount() const { return closed ? p2.size() : (p2.empty() ? 0 : p2.size() - 1); }
    float total() const { return curve ? (isArc ? a1 - a0 : kTwoPi) : cum.back(); }
    // 호의 각을 [a0, a0 + 2π) 로
    float normArc(float a) const { return a0 + wrap2pi(a - a0); }
    bool inRange(float a) const { return !isArc || normArc(a) <= a1 + 1e-5f; }
    float angleOf(const vec3& p) const {
        const vec3 v = p - c3;
        return std::atan2(dot(v, ry), dot(v, rx));
    }
    vec3 pointAt(float a) const { return c3 + (rx * std::cos(a) + ry * std::sin(a)) * r; }
};

struct Plane {
    vec3 right, up, normal;
    V2 proj(const vec3& p) const { return V2{dot(p, right), dot(p, up)}; }
    vec3 lift(V2 q, float w) const { return right * q.x + up * q.y + normal * w; }
};

vec3 worldDir(const LotGameObject& o, const vec3& local) {
    return transformPoint(o.transform.mat4Transform(), local) - o.transform.translation;
}

bool makeShape(const LotGameObject& o, LotGameObject::id_t id, const Plane& pl, Shape& s) {
    if (!o.isSketch()) return false;
    s = Shape{};
    s.id = id;
    if (o.hasCurve()) {
        s.curve = true;
        s.c3 = transformPoint(o.transform.mat4Transform(), o.curve.center);
        const vec3 rxw = worldDir(o, o.curve.right), ryw = worldDir(o, o.curve.up);
        if (len3(rxw) < 1e-12f || len3(ryw) < 1e-12f) return false;
        s.r = o.curve.radius * len3(rxw);
        s.rx = normalize(rxw);
        s.ry = normalize(ryw);
        s.isArc = o.curve.kind == LotGameObject::Curve::Kind::Arc;
        s.a0 = s.isArc ? o.curve.start : 0.0f;
        s.a1 = s.isArc ? o.curve.end : kTwoPi;
        if (s.isArc && s.a1 < s.a0) s.a1 += kTwoPi;
        s.c2 = pl.proj(s.c3);
        s.lo = V2{s.c2.x - s.r, s.c2.y - s.r};
        s.hi = V2{s.c2.x + s.r, s.c2.y + s.r};
        // 미리보기 · 가까운 점 찾기용 점들도 둔다
        s.p3 = o.worldPoints();
        for (const vec3& p : s.p3) s.p2.push_back(pl.proj(p));
        return s.r > 0.0f;
    }
    for (const vec3& w : o.worldPoints()) {
        if (s.p3.empty() || len3(w - s.p3.back()) > 1e-7f) s.p3.push_back(w);
    }
    s.closed = o.closed && s.p3.size() > 2;
    if (s.closed && len3(s.p3.front() - s.p3.back()) <= 1e-7f) s.p3.pop_back();
    if (s.p3.size() < 2) return false;
    s.cum.assign(1, 0.0f);
    const size_t n = s.p3.size();
    for (size_t i = 0; i < (s.closed ? n : n - 1); ++i) s.cum.push_back(s.cum.back() + len3(s.p3[(i + 1) % n] - s.p3[i]));
    s.lo = s.hi = pl.proj(s.p3[0]);
    for (const vec3& p : s.p3) {
        const V2 q = pl.proj(p);
        s.p2.push_back(q);
        s.lo = V2{std::fmin(s.lo.x, q.x), std::fmin(s.lo.y, q.y)};
        s.hi = V2{std::fmax(s.hi.x, q.x), std::fmax(s.hi.y, q.y)};
    }
    return true;
}

bool boxesOverlap(const Shape& a, const Shape& b, float pad) {
    return a.lo.x - pad <= b.hi.x && b.lo.x - pad <= a.hi.x && a.lo.y - pad <= b.hi.y && b.lo.y - pad <= a.hi.y;
}

// 선분 a-b 와 c-d. 둘 다 [0,1] 안에서 만나면 (t, u).
bool segSeg(V2 a, V2 b, V2 c, V2 d, float& t, float& u) {
    const V2 r = b - a, s = d - c;
    const float den = cross2(r, s);
    if (std::fabs(den) < 1e-12f * (dot2(r, r) + dot2(s, s))) return false;
    const V2 ca = c - a;
    t = cross2(ca, s) / den;
    u = cross2(ca, r) / den;
    constexpr float e = 1e-6f;
    return t >= -e && t <= 1.0f + e && u >= -e && u <= 1.0f + e;
}

// 선분 a-b 와 원 (c, r): [0,1] 안의 t 들
int segCircle(V2 a, V2 b, V2 c, float r, float out[2]) {
    const V2 d = b - a, f = a - c;
    const float A = dot2(d, d), B = 2.0f * dot2(f, d), C = dot2(f, f) - r * r;
    if (A < 1e-20f) return 0;
    const float disc = B * B - 4.0f * A * C;
    if (disc < 0.0f) return 0;
    const float sq = std::sqrt(disc);
    int n = 0;
    for (float t : {(-B - sq) / (2.0f * A), (-B + sq) / (2.0f * A)}) {
        if (t >= -1e-6f && t <= 1.0f + 1e-6f && (n == 0 || std::fabs(t - out[0]) > 1e-7f)) out[n++] = t;
    }
    return n;
}

// 두 원의 교점
int circleCircle(V2 c1, float r1, V2 c2, float r2, V2 out[2]) {
    const V2 d = c2 - c1;
    const float dist = std::sqrt(dot2(d, d));
    if (dist < 1e-9f || dist > r1 + r2 + 1e-6f || dist < std::fabs(r1 - r2) - 1e-6f) return 0;
    const float a = (r1 * r1 - r2 * r2 + dist * dist) / (2.0f * dist);
    const float h = std::sqrt(std::fmax(r1 * r1 - a * a, 0.0f));
    const V2 m = c1 + d * (a / dist);
    const V2 perp{-d.y / dist, d.x / dist};
    out[0] = m + perp * h;
    if (h < 1e-7f * r1) return 1;
    out[1] = m - perp * h;
    return 2;
}

// T 와 O 의 교점을 T 의 매개변수로 (폴리선: 누적 길이, 원/호: 원의 각 - 정규화 전).
// fullCircle: T 가 호여도 원 전체로 본다 (호 연장용).
void intersections(const Shape& T, const Shape& O, const Plane& pl, bool fullCircle, std::vector<float>& out) {
    const float wT = T.curve ? dot(T.c3, pl.normal) : dot(T.p3[0], pl.normal);
    if (!T.curve) {
        for (size_t i = 0; i < T.segCount(); ++i) {
            const V2 a = T.p2[i], b = T.p2[(i + 1) % T.p2.size()];
            const float segLen = T.cum[i + 1] - T.cum[i];
            if (!O.curve) {
                for (size_t j = 0; j < O.segCount(); ++j) {
                    float t, u;
                    if (segSeg(a, b, O.p2[j], O.p2[(j + 1) % O.p2.size()], t, u)) {
                        out.push_back(T.cum[i] + std::fmin(std::fmax(t, 0.0f), 1.0f) * segLen);
                    }
                }
            } else {
                float ts[2];
                const int n = segCircle(a, b, O.c2, O.r, ts);
                for (int k = 0; k < n; ++k) {
                    const vec3 p = pl.lift(a + (b - a) * ts[k], wT);
                    if (O.inRange(O.angleOf(p))) out.push_back(T.cum[i] + ts[k] * segLen);
                }
            }
        }
        return;
    }
    auto accept = [&](V2 q) {
        const float a = T.angleOf(pl.lift(q, wT));
        if (fullCircle || T.inRange(a)) out.push_back(a);
    };
    if (!O.curve) {
        for (size_t j = 0; j < O.segCount(); ++j) {
            const V2 a = O.p2[j], b = O.p2[(j + 1) % O.p2.size()];
            float ts[2];
            const int n = segCircle(a, b, T.c2, T.r, ts);
            for (int k = 0; k < n; ++k) accept(a + (b - a) * ts[k]);
        }
    } else {
        V2 q[2];
        const int n = circleCircle(T.c2, T.r, O.c2, O.r, q);
        for (int k = 0; k < n; ++k) {
            if (O.inRange(O.angleOf(pl.lift(q[k], dot(O.c3, pl.normal))))) accept(q[k]);
        }
    }
}

// 폴리선 위 s 위치의 점 (닫혔으면 둘레로 감는다)
vec3 polyPointAt(const Shape& s, float at) {
    const size_t n = s.p3.size();
    if (s.closed) at = std::fmod(std::fmod(at, s.total()) + s.total(), s.total());
    for (size_t i = 0; i < s.segCount(); ++i) {
        if (at <= s.cum[i + 1] || i + 1 == s.segCount()) {
            const float len = s.cum[i + 1] - s.cum[i];
            const float t = len > 0.0f ? (at - s.cum[i]) / len : 0.0f;
            return s.p3[i] + (s.p3[(i + 1) % n] - s.p3[i]) * std::fmin(std::fmax(t, 0.0f), 1.0f);
        }
    }
    return s.p3.back();
}

// 폴리선의 s0 -> s1 부분 (s1 < s0 이면 닫힌 것을 한 바퀴 감아서)
std::vector<vec3> polySub(const Shape& s, float s0, float s1) {
    std::vector<vec3> out{polyPointAt(s, s0)};
    const size_t n = s.p3.size();
    const float L = s.total();
    auto addRange = [&](float from, float to) {   // from < to, 정점들만
        for (size_t i = 0; i <= n; ++i) {
            if (i >= s.cum.size()) break;
            if (s.cum[i] > from + 1e-7f && s.cum[i] < to - 1e-7f) out.push_back(s.p3[i % n]);
        }
    };
    if (s1 >= s0) {
        addRange(s0, s1);
    } else {   // 닫힌 폴리선 감기
        addRange(s0, L);
        out.push_back(s.p3[0]);
        addRange(0.0f, s1);
    }
    out.push_back(polyPointAt(s, s1));
    return out;
}

TrimTool::Piece arcPiece(const Shape& s, float b0, float b1) {
    TrimTool::Piece p;
    p.curve.kind = LotGameObject::Curve::Kind::Arc;
    p.curve.center = s.c3;
    p.curve.right = s.rx;
    p.curve.up = s.ry;
    p.curve.radius = s.r;
    p.curve.start = b0;
    p.curve.end = b1;
    p.points = tessellateArc(s.c3, s.r, s.rx, s.ry, b0, b1, true);
    return p;
}

void addLines(std::vector<std::pair<vec3, vec3>>& dst, const std::vector<vec3>& pts) {
    for (size_t i = 0; i + 1 < pts.size(); ++i) dst.emplace_back(pts[i], pts[i + 1]);
}

// 큰 도면에서 클릭/커서마다 모든 객체를 투영하면 무겁다 - 편집이 있을 때만 다시 만든다
struct ShapeCache {
    const LotGameObject::Map* objects = nullptr;
    uint64_t revision = ~0ull;
    size_t count = 0;
    vec3 normal{};
    std::vector<Shape> shapes;
    std::unordered_map<LotGameObject::id_t, size_t> index;
};
ShapeCache g_cache;
uint64_t g_revision = 0;   // update 가 history 에서 받아 둔다

const ShapeCache& shapes(const LotGameObject::Map& objects, const Plane& pl) {
    if (g_cache.objects == &objects && g_cache.revision == g_revision && g_cache.count == objects.size()
        && len3(g_cache.normal - pl.normal) < 1e-6f) {
        return g_cache;
    }
    g_cache.objects = &objects;
    g_cache.revision = g_revision;
    g_cache.count = objects.size();
    g_cache.normal = pl.normal;
    g_cache.shapes.clear();
    g_cache.index.clear();
    for (const auto& entry : objects) {
        if (!lot_pick::isSelectable(entry.second)) continue;
        Shape s;
        if (makeShape(entry.second, entry.first, pl, s)) {
            g_cache.index[entry.first] = g_cache.shapes.size();
            g_cache.shapes.push_back(std::move(s));
        }
    }
    return g_cache;
}

}  // namespace

void TrimTool::start(Mode mode, const LotCamera& camera) {
    const SketchPlane plane = SketchPlane::fromCamera(camera);
    right_ = plane.right;
    up_ = plane.up;
    normal_ = plane.normal;
    mode_ = mode;
    lastX_ = lastY_ = -1e9f;
    hover_ = Plan{};
    LOT_LOG((mode == Mode::Trim ? "trim" : "extend") << ": click the part to "
            << (mode == Mode::Trim ? "cut away" : "extend") << " (Shift+click does the other), Esc/Enter ends");
}

void TrimTool::cancel() {
    if (!isActive()) return;
    LOT_LOG((mode_ == Mode::Trim ? "trim" : "extend") << ": finished");
    mode_ = Mode::None;
    hover_ = Plan{};
}

TrimTool::Plan TrimTool::plan(const Context& ctx, bool extend) const {
    Plan out;
    const Plane pl{right_, up_, normal_};
    float dist = 0.0f;
    const auto id = lot_pick::pickSketch(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height,
                                         8.0f, ctx.objects, dist);
    const ShapeCache& cache = shapes(ctx.objects, pl);
    const auto it = cache.index.find(id);
    if (it == cache.index.end()) {
        out.why = "no line, polyline, circle or arc there";
        return out;
    }
    const Shape& T = cache.shapes[it->second];
    out.target = id;

    // 커서에 가장 가까운 T 위의 점 (화면 거리) -> T 의 매개변수
    float best = std::numeric_limits<float>::max();
    vec3 hit = T.p3[0];
    const size_t n = T.p3.size();
    const size_t segs = (T.curve ? n - 1 : T.segCount());
    for (size_t i = 0; i < segs; ++i) {
        const vec3& a = T.p3[i];
        const vec3& b = T.p3[(i + 1) % n];
        float ax, ay, bx, by;
        if (!ctx.camera.projectToScreen(a, ctx.width, ctx.height, ax, ay)) continue;
        if (!ctx.camera.projectToScreen(b, ctx.width, ctx.height, bx, by)) continue;
        const float ex = bx - ax, ey = by - ay, ee = ex * ex + ey * ey;
        float t = ee > 0.0f ? ((ctx.mouse.x() - ax) * ex + (ctx.mouse.y() - ay) * ey) / ee : 0.0f;
        t = std::fmin(std::fmax(t, 0.0f), 1.0f);
        const float dx = ax + ex * t - ctx.mouse.x(), dy = ay + ey * t - ctx.mouse.y();
        const float d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            hit = a + (b - a) * t;
        }
    }
    float p;   // 클릭 위치의 매개변수
    if (T.curve) {
        p = T.isArc ? T.normArc(T.angleOf(hit)) : wrap2pi(T.angleOf(hit));
    } else {
        p = 0.0f;
        float bd = std::numeric_limits<float>::max();
        for (size_t i = 0; i < T.segCount(); ++i) {
            const vec3 a = T.p3[i], b = T.p3[(i + 1) % n];
            const vec3 ab = b - a;
            const float l2 = dot(ab, ab);
            float t = l2 > 0.0f ? dot(hit - a, ab) / l2 : 0.0f;
            t = std::fmin(std::fmax(t, 0.0f), 1.0f);
            const float d = len3(a + ab * t - hit);
            if (d < bd) { bd = d; p = T.cum[i] + t * (T.cum[i + 1] - T.cum[i]); }
        }
    }

    // 경계: T 를 빼고 경계상자가 겹치는 것들 (연장은 광선/원 전체라 아래에서 따로)
    const float pad = (T.hi.x - T.lo.x + T.hi.y - T.lo.y) * 1e-6f + 1e-6f;

    if (!extend) {
        std::vector<float> I;
        for (const Shape& O : cache.shapes) {
            if (O.id == T.id || !boxesOverlap(T, O, pad)) continue;
            intersections(T, O, pl, false, I);
        }
        // 매개변수 정규화 + 정렬 + 같은 점 합치기
        for (float& v : I) v = T.curve ? (T.isArc ? T.normArc(v) : wrap2pi(v)) : v;
        std::sort(I.begin(), I.end());
        const float eps = T.total() * 1e-5f + 1e-7f;
        std::vector<float> U;
        for (float v : I) if (U.empty() || v - U.back() > eps) U.push_back(v);
        const bool closed = T.curve ? !T.isArc : T.closed;
        const float start = T.curve && T.isArc ? T.a0 : 0.0f;
        const float end = start + T.total();
        // 열린 것은 끝점에 붙은 교점은 자를 거리가 없다
        if (!closed) U.erase(std::remove_if(U.begin(), U.end(), [&](float v) { return v <= start + eps || v >= end - eps; }), U.end());
        if (U.empty() || (closed && U.size() < 2)) {
            out.kind = Plan::Kind::Delete;   // 빠른 모드: 경계가 없으면 통째로 지운다
            for (size_t i = 0; i < segs; ++i) out.preview.emplace_back(T.p3[i], T.p3[(i + 1) % n]);
            return out;
        }
        float lo = start, hi = end;
        bool haveLo = false, haveHi = false;
        for (float v : U) {
            if (v < p) { lo = v; haveLo = true; }
            if (v > p && !haveHi) { hi = v; haveHi = true; }
        }
        if (closed) {
            if (!haveLo) lo = U.back();
            if (!haveHi) hi = U.front();
        }
        auto sub = [&](float a, float b) -> Piece {
            if (T.curve) {
                if (b <= a) b += kTwoPi;
                return arcPiece(T, a, b);
            }
            Piece pc;
            pc.points = polySub(T, a, b);
            return pc;
        };
        out.kind = Plan::Kind::Replace;
        if (closed) {
            out.pieces.push_back(sub(hi, lo));   // 남는 것: hi 에서 한 바퀴 돌아 lo 까지
            addLines(out.preview, sub(lo, hi).points);
        } else {
            if (haveLo) out.pieces.push_back(sub(start, lo));
            if (haveHi) out.pieces.push_back(sub(hi, end));
            addLines(out.preview, sub(haveLo ? lo : start, haveHi ? hi : end).points);
            if (out.pieces.empty()) out.kind = Plan::Kind::Delete;
        }
        return out;
    }

    // ---- 연장 ----
    if (T.curve ? !T.isArc : T.closed) {
        out.why = "closed shapes have no end to extend";
        return out;
    }
    const float w = T.curve ? dot(T.c3, pl.normal) : dot(T.p3[0], pl.normal);
    if (!T.curve) {
        const bool atStart = p < T.total() * 0.5f;
        const vec3 E = atStart ? T.p3.front() : T.p3.back();
        const vec3 prev = atStart ? T.p3[1] : T.p3[n - 2];
        const vec3 D3 = E - prev;
        const V2 q = pl.proj(E);
        V2 d = pl.proj(D3);
        const float L2 = std::sqrt(dot2(d, d));
        if (L2 < 1e-9f) { out.why = "the end segment is perpendicular to the view"; return out; }
        d = d * (1.0f / L2);
        float bestT = std::numeric_limits<float>::max();
        const float epsT = (T.hi.x - T.lo.x + T.hi.y - T.lo.y) * 1e-5f + 1e-6f;
        for (const Shape& O : cache.shapes) {
            if (O.id == T.id) continue;
            // 광선 대 경계상자: 상자 앞쪽에 이미 더 가까운 교점이 있으면 건너뛴다
            if (!O.curve) {
                for (size_t j = 0; j < O.segCount(); ++j) {
                    const V2 a = O.p2[j], b = O.p2[(j + 1) % O.p2.size()];
                    const V2 s = b - a;
                    const float den = cross2(d, s);
                    if (std::fabs(den) < 1e-12f) continue;
                    const V2 aq = a - q;
                    const float t = cross2(aq, s) / den, u = cross2(aq, d) / den;
                    if (t > epsT && u >= -1e-6f && u <= 1.0f + 1e-6f && t < bestT) bestT = t;
                }
            } else {
                const V2 f = q - O.c2;
                const float B = 2.0f * dot2(f, d), C = dot2(f, f) - O.r * O.r;
                const float disc = B * B - 4.0f * C;
                if (disc < 0.0f) continue;
                const float sq = std::sqrt(disc);
                for (float t : {(-B - sq) * 0.5f, (-B + sq) * 0.5f}) {
                    if (t <= epsT || t >= bestT) continue;
                    if (O.inRange(O.angleOf(pl.lift(q + d * t, dot(O.c3, pl.normal))))) bestT = t;
                }
            }
        }
        if (bestT == std::numeric_limits<float>::max()) { out.why = "no boundary in that direction"; return out; }
        const vec3 newEnd = E + D3 * (bestT / L2);
        out.kind = Plan::Kind::Extend;
        out.extended.points = T.p3;
        (atStart ? out.extended.points.front() : out.extended.points.back()) = newEnd;
        out.extended.closed = false;
        out.preview.emplace_back(E, newEnd);
        return out;
    }
    // 호: 원을 따라 가장 가까운 경계까지 (끝 쪽이면 각이 커지는 쪽, 시작 쪽이면 작아지는 쪽)
    const bool atStart = (p - T.a0) < (T.a1 - p);
    std::vector<float> I;
    for (const Shape& O : cache.shapes) {
        if (O.id == T.id) continue;
        Shape full = T;
        full.isArc = false;
        full.lo = V2{T.c2.x - T.r, T.c2.y - T.r};
        full.hi = V2{T.c2.x + T.r, T.c2.y + T.r};
        if (!boxesOverlap(full, O, pad)) continue;
        intersections(full, O, pl, true, I);
    }
    const float gap = kTwoPi - (T.a1 - T.a0);   // 원을 다 채우기 전까지만
    float bestD = std::numeric_limits<float>::max();
    for (float a : I) {
        const float dlt = atStart ? wrap2pi(T.a0 - a) : wrap2pi(a - T.a1);
        if (dlt > 1e-5f && dlt < gap - 1e-5f && dlt < bestD) bestD = dlt;
    }
    if (bestD == std::numeric_limits<float>::max()) { out.why = "no boundary along the circle"; return out; }
    out.kind = Plan::Kind::Extend;
    const float b0 = atStart ? T.a0 - bestD : T.a0;
    const float b1 = atStart ? T.a1 : T.a1 + bestD;
    out.extended = arcPiece(T, b0, b1);
    addLines(out.preview, (atStart ? arcPiece(T, b0, T.a0) : arcPiece(T, T.a1, b1)).points);
    (void)w;
    return out;
}

void TrimTool::update(const Context& ctx, EditHistory& history) {
    if (!isActive()) return;
    g_revision = history.revision();

    // 미리보기: 커서가 움직였을 때만 다시 계산
    if (std::fabs(ctx.mouse.x() - lastX_) + std::fabs(ctx.mouse.y() - lastY_) > 1.5f) {
        lastX_ = ctx.mouse.x();
        lastY_ = ctx.mouse.y();
        hover_ = plan(ctx, mode_ == Mode::Extend);
    }

    if (!ctx.mouse.consumeLeftPress()) { ctx.mouse.consumeLeftRelease(); return; }
    ctx.mouse.consumeLeftRelease();
    const bool extend = (mode_ == Mode::Extend) != ctx.mouse.shiftAtPress();
    const Plan pl = plan(ctx, extend);
    const char* name = extend ? "extend" : "trim";
    if (pl.kind == Plan::Kind::None) {
        LOT_LOG(name << ": " << (pl.why.empty() ? "nothing to do" : pl.why));
        return;
    }
    LotGameObject* src = LotGameObject::find(ctx.objects, pl.target);
    if (!src) return;

    EditHistory::Edit edit;
    edit.label = name;
    edit.before.push_back(EditHistory::snapshot(ctx.objects, pl.target));

    if (pl.kind == Plan::Kind::Extend) {
        // 제자리 고치기: 변환은 그대로, 점(과 호의 각)만
        if (pl.extended.curve.kind != LotGameObject::Curve::Kind::None) {
            src->curve.start = pl.extended.curve.start;
            src->curve.end = pl.extended.curve.end;
            src->points = tessellateArc(src->curve.center, src->curve.radius, src->curve.right, src->curve.up,
                                        src->curve.start, src->curve.end, true);
        } else {
            src->points.clear();
            for (const vec3& w : pl.extended.points) src->points.push_back(src->transform.worldToLocalPoint(w));
        }
        edit.after.push_back(EditHistory::snapshot(ctx.objects, pl.target));
        history.record(std::move(edit));
        LOT_LOG("extend: object " << pl.target << " extended");
    } else {
        // 남는 조각들을 새 객체로 (원본의 층 · 색 · 선종류), 원본은 지운다
        std::set<LotGameObject::id_t> made;
        for (const Piece& pc : pl.pieces) {
            if (pc.points.size() < 2) continue;
            LotGameObject obj = LotGameObject::createGameObject();
            vec3 origin{0.0f, 0.0f, 0.0f};
            if (pc.curve.kind != LotGameObject::Curve::Kind::None) {
                origin = pc.curve.center;
            } else {
                for (const vec3& q : pc.points) origin = origin + q;
                origin = origin * (1.0f / static_cast<float>(pc.points.size()));
            }
            obj.transform.translation = origin;
            obj.color = src->color;
            obj.colorByLayer = src->colorByLayer;
            obj.layer = src->layer;
            obj.linetype = src->linetype;
            obj.closed = pc.closed;
            for (const vec3& q : pc.points) obj.points.push_back(q - origin);
            if (pc.curve.kind != LotGameObject::Curve::Kind::None) {
                obj.curve = pc.curve;
                obj.curve.center = vec3{0.0f, 0.0f, 0.0f};
            }
            const auto nid = obj.getId();
            ctx.objects.emplace(nid, std::move(obj));
            made.insert(nid);
        }
        ctx.objects.erase(pl.target);
        edit.after = EditHistory::snapshot(ctx.objects, made);
        history.record(std::move(edit));
        LOT_LOG("trim: object " << pl.target << (pl.kind == Plan::Kind::Delete ? " deleted (no cutting edge)"
                                                 : " cut into " + std::to_string(made.size()) + " pieces"));
    }
    g_revision = history.revision();
    lastX_ = lastY_ = -1e9f;   // 다음 프레임에 미리보기 다시
}

std::string TrimTool::hint() const {
    if (mode_ == Mode::Trim) return "trim: click the part to cut away (Shift+click extends)  [Esc/Enter ends]";
    if (mode_ == Mode::Extend) return "extend: click near the end to extend (Shift+click trims)  [Esc/Enter ends]";
    return "";
}

void TrimTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    if (!isActive()) return;
    const vec3 color = (hover_.kind == Plan::Kind::Extend) ? kExtendColor : kRemoveColor;
    // 지울 부분은 원래 선과 같은 자리라 뎁스가 같다 - 나중에 그려지는 선에 덮이지 않게 카메라 쪽으로
    // 거리의 0.2% 당긴다 (선택 강조와 같은 방법, 화면에서는 안 보이는 만큼)
    const vec3 eye = ctx.camera.getPosition();
    auto lift = [&](const vec3& p) { return p + (eye - p) * 0.002f; };
    for (const auto& seg : hover_.preview) lines.addLine(lift(seg.first), lift(seg.second), color);
}
