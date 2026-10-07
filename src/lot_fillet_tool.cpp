#include "lot_fillet_tool.h"
#include "line_render_system.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "lot_picking.h"
#include "lot_sketch_tool.h"   // SketchPlane, tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530718f;
const vec3 kFirstColor{1.0f, 0.85f, 0.2f};
const vec3 kPreviewColor{0.4f, 0.8f, 1.0f};

float len3(const vec3& v) { return std::sqrt(dot(v, v)); }
float wrap2pi(float a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0f ? a + kTwoPi : a;
}

// 객체의 월드 점 (이어진 같은 점은 버린다). 닫힌 것은 첫 점을 끝에 다시 두지 않는다.
std::vector<vec3> cleanPoints(const LotGameObject& o, bool& closed) {
    std::vector<vec3> p;
    for (const vec3& w : o.worldPoints()) {
        if (p.empty() || len3(w - p.back()) > 1e-7f) p.push_back(w);
    }
    closed = o.closed && p.size() > 2;
    if (closed && len3(p.front() - p.back()) <= 1e-7f) p.pop_back();
    return p;
}

}  // namespace

void FilletTool::start(Mode mode, const LotCamera& camera, float defaultValue) {
    const SketchPlane plane = SketchPlane::fromCamera(camera);
    right_ = plane.right;
    up_ = plane.up;
    normal_ = plane.normal;
    if (mode == Mode::Fillet && !radiusSet_ && defaultValue > 0.0f) radius_ = defaultValue;
    if (mode == Mode::Chamfer && !distanceSet_ && defaultValue > 0.0f) distance_ = defaultValue;
    mode_ = mode;
    state_ = State::PickFirst;
    number_.clear();
    first_ = Pick{};
    hover_ = Plan{};
    lastX_ = lastY_ = -1e9f;
    LOT_LOG((mode == Mode::Fillet ? "fillet R=" : "chamfer D=") << (mode == Mode::Fillet ? radius_ : distance_)
            << ": type a value + Enter or click the first line (Esc ends)");
}

void FilletTool::cancel() {
    if (!isActive()) return;
    LOT_LOG((mode_ == Mode::Fillet ? "fillet" : "chamfer") << ": finished");
    mode_ = Mode::None;
    state_ = State::Idle;
    number_.clear();
    hover_ = Plan{};
}

void FilletTool::finish() {
    if (!isActive()) return;
    if (number_.empty()) {   // 값 없이 Enter = 끝
        cancel();
        return;
    }
    const float v = std::strtof(number_.c_str(), nullptr);
    number_.clear();
    if (!(v >= 0.0f)) {
        LOT_LOG("fillet: the value must be 0 or more");
        return;
    }
    if (mode_ == Mode::Fillet) { radius_ = v; radiusSet_ = true; }
    else { distance_ = v; distanceSet_ = true; }
    lastX_ = lastY_ = -1e9f;
    LOT_LOG((mode_ == Mode::Fillet ? "fillet: radius " : "chamfer: distance ") << v);
}

bool FilletTool::pickAt(const Context& ctx, Pick& out) const {
    float dist = 0.0f;
    const auto id = lot_pick::pickSketch(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height,
                                         8.0f, ctx.objects, dist);
    const LotGameObject* o = LotGameObject::find(ctx.objects, id);
    if (!o || !o->isSketch() || o->hasCurve() || o->isHatch()) return false;
    bool closed = false;
    const std::vector<vec3> p = cleanPoints(*o, closed);
    if (p.size() < 2) return false;
    const size_t segs = closed ? p.size() : p.size() - 1;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < segs; ++i) {
        const vec3& a = p[i];
        const vec3& b = p[(i + 1) % p.size()];
        float ax, ay, bx, by;
        if (!ctx.camera.projectToScreen(a, ctx.width, ctx.height, ax, ay)) continue;
        if (!ctx.camera.projectToScreen(b, ctx.width, ctx.height, bx, by)) continue;
        const float ex = bx - ax, ey = by - ay, ee = ex * ex + ey * ey;
        float t = ee > 0.0f ? ((ctx.mouse.x() - ax) * ex + (ctx.mouse.y() - ay) * ey) / ee : 0.0f;
        t = std::fmin(std::fmax(t, 0.0f), 1.0f);
        const float dx = ax + ex * t - ctx.mouse.x(), dy = ay + ey * t - ctx.mouse.y();
        if (dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            out.id = id;
            out.segment = i;
            out.point = a + (b - a) * t;
        }
    }
    return out.id != LotGameObject::kInvalidId;
}

FilletTool::Plan FilletTool::plan(const Pick& pa, const Pick& pb, const LotGameObject::Map& objects) const {
    Plan out;
    const bool fillet = mode_ == Mode::Fillet;
    const float value = fillet ? radius_ : distance_;
    const LotGameObject* oa = LotGameObject::find(objects, pa.id);
    const LotGameObject* ob = LotGameObject::find(objects, pb.id);
    if (!oa || !ob) { out.why = "pick two lines"; return out; }

    // 2D (작업평면) <-> 3D
    auto proj = [&](const vec3& p) { return vec3{dot(p, right_), dot(p, up_), 0.0f}; };
    auto lift = [&](const vec3& q, float w) { return right_ * q.x + up_ * q.y + normal_ * w; };
    auto dir2 = [&](const vec3& from, const vec3& to) {
        const vec3 d = proj(to) - proj(from);
        const float l = len3(d);
        return l > 1e-12f ? d * (1.0f / l) : vec3{0.0f, 0.0f, 0.0f};
    };

    // 모서리 X 에서 u1, u2 방향으로 갈 때의 접점 / 호(또는 모따기 선) 점들 (월드).
    // reach1/2: X 에서 그 쪽 끝까지 남은 길이 - 접점이 그보다 멀면 반지름이 너무 크다.
    auto corner = [&](const vec3& X3, const vec3& u1, const vec3& u2, float reach1, float reach2,
                      vec3& T1, vec3& T2, std::vector<vec3>& mid, LotGameObject::Curve& curve) -> bool {
        const float w = dot(X3, normal_);
        const vec3 X = proj(X3);
        const float c = std::fmin(std::fmax(dot(u1, u2), -1.0f), 1.0f);
        const float theta = std::acos(c);   // 두 남는 쪽 사이 각
        if (theta < 1e-4f || theta > kPi - 1e-4f) { out.why = "the lines are collinear"; return false; }
        float t1 = value, t2 = value;
        if (fillet) t1 = t2 = value / std::tan(theta * 0.5f);
        if (t1 > reach1 + 1e-6f || t2 > reach2 + 1e-6f) {
            out.why = fillet ? "the radius is too large for these lines" : "the distance is longer than the line";
            return false;
        }
        T1 = lift(X + u1 * t1, w);
        T2 = lift(X + u2 * t2, w);
        mid.clear();
        if (!fillet || value <= 0.0f) return true;   // 모따기 / R0: 곧은 선 또는 맞붙임
        const vec3 bis = normalize(u1 + u2);
        const vec3 C = X + bis * (value / std::sin(theta * 0.5f));
        const vec3 C3 = lift(C, w);
        const vec3 d1 = proj(T1) - C, d2 = proj(T2) - C;
        const float a1 = std::atan2(d1.y, d1.x), a2 = std::atan2(d2.y, d2.x);
        // 짧은 쪽으로 (접호는 늘 반원보다 작다). T1 -> T2 순서의 점들을 만든다.
        const float sweep = wrap2pi(a2 - a1);
        const bool ccw = sweep <= kPi;
        curve.kind = LotGameObject::Curve::Kind::Arc;
        curve.center = C3;
        curve.right = right_;
        curve.up = up_;
        curve.radius = value;
        curve.start = ccw ? a1 : a2;
        curve.end = ccw ? a1 + sweep : a2 + wrap2pi(a1 - a2);
        mid = tessellateArc(C3, value, right_, up_, curve.start, curve.end, true);
        if (!ccw) std::reverse(mid.begin(), mid.end());
        return true;
    };

    bool ca = false, cb = false;
    std::vector<vec3> P = cleanPoints(*oa, ca);
    std::vector<vec3> Q = cleanPoints(*ob, cb);

    // ---- 같은 폴리선의 이웃한 두 변: 꼭짓점 자리에 호를 끼운다 ----
    if (pa.id == pb.id) {
        const size_t n = P.size();
        const size_t segs = ca ? n : n - 1;
        size_t i = pa.segment, j = pb.segment, k = n;   // k = 공유 꼭짓점
        if (j == (i + 1) % segs || (!ca && j == i + 1)) k = j;
        else if (i == (j + 1) % segs || (!ca && i == j + 1)) k = i;
        if (k == n || (!ca && (k == 0 || k == n - 1))) { out.why = "pick two adjacent segments of the polyline"; return out; }
        const vec3 V = P[k], prev = P[(k + n - 1) % n], next = P[(k + 1) % n];
        vec3 T1, T2;
        std::vector<vec3> mid;
        LotGameObject::Curve unused;
        if (!corner(V, dir2(V, prev), dir2(V, next), len3(proj(prev) - proj(V)), len3(proj(next) - proj(V)),
                    T1, T2, mid, unused)) {
            return out;
        }
        std::vector<vec3> np(P.begin(), P.begin() + static_cast<long>(k));
        np.push_back(T1);
        for (size_t m = 1; m + 1 < mid.size(); ++m) np.push_back(mid[m]);
        if (len3(T2 - T1) > 1e-7f) np.push_back(T2);
        for (size_t m = k + 1; m < n; ++m) np.push_back(P[m]);
        out.modified.emplace_back(pa.id, np);
        for (size_t m = 0; m + 1 < np.size() + (ca ? 1 : 0); ++m) out.preview.emplace_back(np[m], np[(m + 1) % np.size()]);
        out.ok = true;
        return out;
    }

    // ---- 서로 다른 두 객체: 선 또는 열린 폴리선의 끝 변 ----
    struct Side {
        const std::vector<vec3>* pts;
        bool closed;
        size_t seg;
        vec3 pick;
        size_t moveIdx = 0;   // 접점으로 옮길 끝점
        size_t keepIdx = 0;   // 남는 쪽의 이웃 점 (방향)
    };
    Side A{&P, ca, pa.segment, pa.point}, B{&Q, cb, pb.segment, pb.point};
    for (Side* s : {&A, &B}) {
        const size_t n = s->pts->size();
        if (s->closed) { out.why = "for a closed polyline pick two of its own adjacent segments"; return out; }
        if (n > 2 && s->seg != 0 && s->seg != n - 2) { out.why = "pick an end segment of the polyline"; return out; }
    }
    // 두 변의 무한 직선 교점
    const vec3 a0 = (*A.pts)[A.seg], a1 = (*A.pts)[A.seg + 1];
    const vec3 b0 = (*B.pts)[B.seg], b1 = (*B.pts)[B.seg + 1];
    const vec3 ra = proj(a1) - proj(a0), rb = proj(b1) - proj(b0);
    const float den = ra.x * rb.y - ra.y * rb.x;
    if (std::fabs(den) < 1e-12f * (dot(ra, ra) + dot(rb, rb))) { out.why = "the lines are parallel"; return out; }
    const vec3 ab = proj(b0) - proj(a0);
    const float ta = (ab.x * rb.y - ab.y * rb.x) / den;
    const vec3 X2 = proj(a0) + ra * ta;
    const vec3 X3 = lift(X2, dot(a0, normal_));

    // 어느 쪽을 남기나: 폴리선은 안쪽(나머지가 붙은 쪽), 2점 선은 클릭한 쪽
    for (Side* s : {&A, &B}) {
        const std::vector<vec3>& p = *s->pts;
        const size_t n = p.size();
        if (n == 2) {
            const vec3 c = proj(s->pick) - X2;
            const float d0 = dot(proj(p[0]) - X2, c), d1 = dot(proj(p[1]) - X2, c);
            s->keepIdx = d0 >= d1 ? 0 : 1;
            s->moveIdx = 1 - s->keepIdx;
        } else {
            s->moveIdx = (s->seg == 0) ? 0 : n - 1;
            s->keepIdx = (s->seg == 0) ? 1 : n - 2;
        }
    }
    const vec3 u1 = dir2(X3, (*A.pts)[A.keepIdx]);
    const vec3 u2 = dir2(X3, (*B.pts)[B.keepIdx]);
    if (len3(u1) < 0.5f || len3(u2) < 0.5f) { out.why = "click away from the corner"; return out; }
    // 남는 쪽 끝까지의 길이 (2점 선은 그 끝점, 폴리선은 끝 변의 안쪽 점)
    const float reachA = dot(proj((*A.pts)[A.keepIdx]) - X2, u1);
    const float reachB = dot(proj((*B.pts)[B.keepIdx]) - X2, u2);
    vec3 T1, T2;
    std::vector<vec3> mid;
    if (!corner(X3, u1, u2, reachA, reachB, T1, T2, mid, out.createdCurve)) return out;

    std::vector<vec3> na = P, nb = Q;
    na[A.moveIdx] = T1;
    nb[B.moveIdx] = T2;
    out.modified.emplace_back(pa.id, na);
    out.modified.emplace_back(pb.id, nb);
    if (fillet && value > 0.0f) out.createdPoints = mid;                       // 호
    else if (!fillet && value > 0.0f) out.createdPoints = {T1, T2};            // 모따기 선
    for (const auto& m : out.modified) {
        for (size_t i = 0; i + 1 < m.second.size(); ++i) out.preview.emplace_back(m.second[i], m.second[i + 1]);
    }
    for (size_t i = 0; i + 1 < out.createdPoints.size(); ++i) out.preview.emplace_back(out.createdPoints[i], out.createdPoints[i + 1]);
    out.ok = true;
    return out;
}

void FilletTool::update(const Context& ctx, EditHistory& history) {
    if (!isActive()) return;
    const char* name = mode_ == Mode::Fillet ? "fillet" : "chamfer";

    // 미리보기 (둘째 선 위에서, 커서가 움직였을 때만)
    if (state_ == State::PickSecond && std::fabs(ctx.mouse.x() - lastX_) + std::fabs(ctx.mouse.y() - lastY_) > 1.5f) {
        lastX_ = ctx.mouse.x();
        lastY_ = ctx.mouse.y();
        Pick b;
        hover_ = (pickAt(ctx, b) && !(b.id == first_.id && b.segment == first_.segment))
                     ? plan(first_, b, ctx.objects) : Plan{};
    }

    if (!ctx.mouse.consumeLeftPress()) { ctx.mouse.consumeLeftRelease(); return; }
    ctx.mouse.consumeLeftRelease();
    Pick p;
    if (!pickAt(ctx, p)) {
        LOT_LOG(name << ": click a line or polyline (arcs and circles are not supported yet)");
        return;
    }
    if (state_ == State::PickFirst) {
        first_ = p;
        state_ = State::PickSecond;
        lastX_ = lastY_ = -1e9f;
        LOT_LOG(name << ": first line " << p.id << " - click the second line");
        return;
    }
    if (p.id == first_.id && p.segment == first_.segment) return;   // 같은 변을 또 누른 것은 무시
    const Plan pl = plan(first_, p, ctx.objects);
    state_ = State::PickFirst;
    hover_ = Plan{};
    if (!pl.ok) {
        LOT_LOG(name << ": " << pl.why);
        return;
    }

    EditHistory::Edit edit;
    edit.label = name;
    std::set<LotGameObject::id_t> touched;
    for (const auto& m : pl.modified) touched.insert(m.first);
    edit.before = EditHistory::snapshot(ctx.objects, touched);
    for (const auto& m : pl.modified) {
        LotGameObject* o = LotGameObject::find(ctx.objects, m.first);
        if (!o) continue;
        o->points.clear();
        for (const vec3& w : m.second) o->points.push_back(o->transform.worldToLocalPoint(w));
    }
    std::set<LotGameObject::id_t> after = touched;
    if (pl.createdPoints.size() >= 2) {
        const LotGameObject* src = LotGameObject::find(ctx.objects, first_.id);
        LotGameObject obj = LotGameObject::createGameObject();
        vec3 origin{0.0f, 0.0f, 0.0f};
        if (pl.createdCurve.kind != LotGameObject::Curve::Kind::None) {
            origin = pl.createdCurve.center;
        } else {
            for (const vec3& q : pl.createdPoints) origin = origin + q;
            origin = origin * (1.0f / static_cast<float>(pl.createdPoints.size()));
        }
        obj.transform.translation = origin;
        if (src) {
            obj.color = src->color;
            obj.colorByLayer = src->colorByLayer;
            obj.layer = src->layer;
            obj.linetype = src->linetype;
        }
        for (const vec3& q : pl.createdPoints) obj.points.push_back(q - origin);
        if (pl.createdCurve.kind != LotGameObject::Curve::Kind::None) {
            obj.curve = pl.createdCurve;
            obj.curve.center = vec3{0.0f, 0.0f, 0.0f};
        }
        const auto nid = obj.getId();
        ctx.objects.emplace(nid, std::move(obj));
        after.insert(nid);
    }
    edit.after = EditHistory::snapshot(ctx.objects, after);
    history.record(std::move(edit));
    LOT_LOG(name << ": done (" << (mode_ == Mode::Fillet ? "R=" : "D=") << (mode_ == Mode::Fillet ? radius_ : distance_)
            << (pl.createdPoints.size() >= 2 ? ", new object" : "") << ")");
}

std::string FilletTool::hint() const {
    if (!isActive()) return "";
    char buf[160];
    const bool f = mode_ == Mode::Fillet;
    std::snprintf(buf, sizeof(buf), "%s %s=%g: %s  [value + Enter changes it, Esc ends]",
                  f ? "fillet" : "chamfer", f ? "R" : "D", f ? radius_ : distance_,
                  state_ == State::PickFirst ? "click the first line" : "click the second line");
    std::string s = buf;
    if (!number_.empty()) s += "   [" + number_ + "]";
    return s;
}

void FilletTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    if (!isActive()) return;
    const vec3 eye = ctx.camera.getPosition();
    auto lift = [&](const vec3& p) { return p + (eye - p) * 0.002f; };   // 원래 선에 덮이지 않게
    if (state_ == State::PickSecond) {
        if (const LotGameObject* o = LotGameObject::find(ctx.objects, first_.id)) {
            const std::vector<vec3> p = o->worldPoints();
            if (first_.segment + 1 < p.size() + (o->closed ? 1 : 0)) {
                lines.addLine(lift(p[first_.segment]), lift(p[(first_.segment + 1) % p.size()]), kFirstColor);
            }
        }
    }
    for (const auto& seg : hover_.preview) lines.addLine(lift(seg.first), lift(seg.second), kPreviewColor);
}
