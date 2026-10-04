#include "lot_offset_tool.h"
#include "line_render_system.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "lot_picking.h"
#include "lot_sketch_tool.h"   // SketchPlane, tessellateArc

#include <cmath>
#include <cstdlib>

namespace {

const vec3 kGhostColor{0.4f, 0.8f, 1.0f};
const vec3 kSourceColor{1.0f, 0.85f, 0.2f};

float len(const vec3& v) { return std::sqrt(dot(v, v)); }

// 객체 축을 월드로 (이동은 빼고)
vec3 worldDir(const LotGameObject& o, const vec3& local) {
    return transformPoint(o.transform.mat4Transform(), local) - o.transform.translation;
}

// 객체가 놓인 평면의 법선. 곡선이면 그 축, 아니면 점들의 뉴웰 법선, 곧은 선이면 fallback.
vec3 objectNormal(const LotGameObject& o, const std::vector<vec3>& pts, const vec3& fallback) {
    if (o.hasCurve()) {
        const vec3 n = cross(worldDir(o, o.curve.right), worldDir(o, o.curve.up));
        if (len(n) > 1e-12f) return normalize(n);
    }
    vec3 n{0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < pts.size(); ++i) {
        const vec3& a = pts[i];
        const vec3& b = pts[(i + 1) % pts.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    // 거의 곧은 폴리선은 뉴웰 법선이 작고 흔들린다 - 그때는 작업평면 법선
    float extent = 0.0f;
    for (const vec3& p : pts) extent = std::fmax(extent, len(p - pts[0]));
    if (len(n) > 1e-6f * extent * extent) return normalize(n);
    return fallback;
}

// 점에서 선분까지 거리
float distToSegment(const vec3& p, const vec3& a, const vec3& b) {
    const vec3 ab = b - a;
    const float l2 = dot(ab, ab);
    float t = l2 > 0.0f ? dot(p - a, ab) / l2 : 0.0f;
    t = std::fmin(std::fmax(t, 0.0f), 1.0f);
    return len(p - (a + ab * t));
}

// 평면 위 두 직선 (p + s u), (q + t v) 의 교점. 거의 평행이면 false.
bool intersectLines(const vec3& p, const vec3& u, const vec3& q, const vec3& v, const vec3& n, vec3& out) {
    const vec3 uxv = cross(u, v);
    const float den = dot(uxv, n);
    if (std::fabs(den) < 1e-9f * len(u) * len(v)) return false;
    const float s = dot(cross(q - p, v), n) / den;
    out = p + u * s;
    return true;
}

}  // namespace

bool OffsetTool::offsetShape(const LotGameObject& src, float d, const vec3& side, const vec3& fallbackNormal,
                             std::vector<vec3>& out, bool& closedOut, LotGameObject::Curve& curveOut) {
    out.clear();
    curveOut = LotGameObject::Curve{};
    closedOut = src.closed;
    if (!src.isSketch() || !(d > 0.0f)) return false;

    if (src.hasCurve()) {
        // 원 / 호: 중심 · 각은 그대로, 반지름만. 커서가 안쪽이면 작아진다.
        const vec3 c = transformPoint(src.transform.mat4Transform(), src.curve.center);
        const vec3 rw = worldDir(src, src.curve.right);
        const vec3 uw = worldDir(src, src.curve.up);
        const float r = src.curve.radius * len(rw);
        const vec3 n = normalize(cross(rw, uw));
        vec3 rel = side - c;
        rel = rel - n * dot(rel, n);
        const float newR = (len(rel) < r) ? r - d : r + d;
        if (newR <= r * 1e-4f) {
            LOT_LOG("offset: the circle would vanish (distance " << d << " >= radius " << r << ")");
            return false;
        }
        curveOut = src.curve;
        curveOut.center = c;
        curveOut.right = normalize(rw);
        curveOut.up = normalize(uw);
        curveOut.radius = newR;
        const bool isArc = src.curve.kind == LotGameObject::Curve::Kind::Arc;
        out = tessellateArc(c, newR, curveOut.right, curveOut.up, curveOut.start, curveOut.end, isArc);
        return out.size() >= 2;
    }

    // 선 / 폴리선. 이어진 같은 점은 버린다 (길이 0 인 변은 방향이 없다).
    std::vector<vec3> p;
    for (const vec3& w : src.worldPoints()) {
        if (p.empty() || len(w - p.back()) > 1e-7f) p.push_back(w);
    }
    if (src.closed && p.size() > 2 && len(p.front() - p.back()) <= 1e-7f) p.pop_back();
    if (p.size() < 2) return false;
    const bool closed = src.closed && p.size() > 2;
    closedOut = closed;
    const vec3 n = objectNormal(src, p, fallbackNormal);

    const size_t m = closed ? p.size() : p.size() - 1;   // 변 개수
    std::vector<vec3> dir(m), left(m);
    for (size_t i = 0; i < m; ++i) {
        dir[i] = p[(i + 1) % p.size()] - p[i];
        left[i] = normalize(cross(n, dir[i]));
    }
    // 어느 쪽인가: 커서에서 가장 가까운 변의 왼쪽/오른쪽
    size_t nearest = 0;
    float best = 1e30f;
    for (size_t i = 0; i < m; ++i) {
        const float dd = distToSegment(side, p[i], p[(i + 1) % p.size()]);
        if (dd < best) { best = dd; nearest = i; }
    }
    const float s = dot(side - p[nearest], left[nearest]) >= 0.0f ? 1.0f : -1.0f;
    const float D = s * d;

    // 꼭짓점 j: 들어오는 변 (j-1) 과 나가는 변 j 의 평행선 교점
    auto corner = [&](size_t in, size_t outIdx, const vec3& vertex, std::vector<vec3>& dst) {
        const vec3 a = p[in] + left[in] * D;          // 들어오는 변의 평행선 위 한 점
        const vec3 b = p[outIdx] + left[outIdx] * D;  // 나가는 변의 평행선 위 한 점
        vec3 x;
        if (!intersectLines(a, dir[in], b, dir[outIdx], n, x)) {
            dst.push_back(vertex + left[outIdx] * D);   // 나란하면 그냥 옮긴 점
            return;
        }
        // 너무 뾰족하면 (꼭짓점에서 거리의 4 배 넘게 튀어나가면) 두 점으로 깎는다
        if (len(x - vertex) > 4.0f * d) {
            dst.push_back(vertex + left[in] * D);
            dst.push_back(vertex + left[outIdx] * D);
            return;
        }
        dst.push_back(x);
    };

    if (closed) {
        for (size_t j = 0; j < p.size(); ++j) corner((j + m - 1) % m, j, p[j], out);
    } else {
        out.push_back(p[0] + left[0] * D);
        for (size_t j = 1; j + 1 < p.size(); ++j) corner(j - 1, j, p[j], out);
        out.push_back(p.back() + left[m - 1] * D);
    }
    return out.size() >= 2;
}

void OffsetTool::start(const LotCamera& camera, float defaultDistance) {
    if (!distanceSet_ && defaultDistance > 0.0f) distance_ = defaultDistance;
    workNormal_ = SketchPlane::fromCamera(camera).normal;
    number_.clear();
    source_ = LotGameObject::kInvalidId;
    state_ = State::WaitingDistance;
    LOT_LOG("offset: type the distance + Enter (Enter alone = " << distance_ << ")");
}

void OffsetTool::cancel() {
    if (!isActive()) return;
    state_ = State::Idle;
    source_ = LotGameObject::kInvalidId;
    number_.clear();
    LOT_LOG("offset: finished");
}

void OffsetTool::finish() {
    if (state_ != State::WaitingDistance) {
        cancel();   // 객체를 고르는 중에 Enter = 끝 (AutoCAD 와 같다)
        return;
    }
    if (!number_.empty()) {
        const float v = std::strtof(number_.c_str(), nullptr);
        if (!(v > 0.0f)) {
            LOT_LOG("offset: distance must be positive");
            number_.clear();
            return;
        }
        distance_ = v;
    }
    distanceSet_ = true;
    number_.clear();
    state_ = State::PickObject;
    LOT_LOG("offset: distance " << distance_ << " - click a line, polyline, circle or arc");
}

bool OffsetTool::sidePoint(const Context& ctx, const LotGameObject& obj, vec3& out) const {
    // 객체 평면과 커서 레이의 교점
    const std::vector<vec3> pts = obj.worldPoints();
    if (pts.empty()) return false;
    const vec3 n = objectNormal(obj, pts, workNormal_);
    const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height);
    const float den = dot(ray.direction, n);
    if (std::fabs(den) < 1e-9f) return false;
    const float t = dot(pts[0] - ray.origin, n) / den;
    out = ray.origin + ray.direction * t;
    return true;
}

void OffsetTool::update(const Context& ctx, EditHistory& history) {
    if (!isActive()) return;
    if (!ctx.mouse.consumeLeftPress()) { ctx.mouse.consumeLeftRelease(); return; }
    ctx.mouse.consumeLeftRelease();

    if (state_ == State::WaitingDistance) {
        finish();   // 거리를 안 치고 바로 객체를 눌렀으면 기본 거리로 - 그 클릭이 곧 객체 고르기
    }
    if (state_ == State::PickObject) {
        float dist = 0.0f;
        const auto id = lot_pick::pickSketch(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height,
                                             8.0f, ctx.objects, dist);
        const LotGameObject* obj = LotGameObject::find(ctx.objects, id);
        if (!obj || !obj->isSketch()) {
            LOT_LOG("offset: no line, polyline, circle or arc there");
            return;
        }
        source_ = id;
        state_ = State::PickSide;
        LOT_LOG("offset: object " << id << " - click the side to offset to");
        return;
    }
    // PickSide
    const LotGameObject* src = LotGameObject::find(ctx.objects, source_);
    vec3 side;
    if (!src || !sidePoint(ctx, *src, side)) { state_ = State::PickObject; return; }
    std::vector<vec3> pts;
    bool closed = false;
    LotGameObject::Curve curve;
    if (!offsetShape(*src, distance_, side, workNormal_, pts, closed, curve)) {
        state_ = State::PickObject;
        return;
    }
    // 새 스케치 객체: 원본의 층 · 색 · 선종류를 물려받는다. 회전/축척 없이 월드 점 그대로.
    LotGameObject obj = LotGameObject::createGameObject();
    vec3 origin{0.0f, 0.0f, 0.0f};
    if (curve.kind != LotGameObject::Curve::Kind::None) {
        origin = curve.center;
    } else {
        for (const vec3& p : pts) origin = origin + p;
        origin = origin * (1.0f / static_cast<float>(pts.size()));
    }
    obj.transform.translation = origin;
    obj.color = src->color;
    obj.colorByLayer = src->colorByLayer;
    obj.layer = src->layer;
    obj.linetype = src->linetype;
    obj.closed = closed;
    obj.points.reserve(pts.size());
    for (const vec3& p : pts) obj.points.push_back(p - origin);
    if (curve.kind != LotGameObject::Curve::Kind::None) {
        obj.curve = curve;
        obj.curve.center = vec3{0.0f, 0.0f, 0.0f};
    }
    const auto id = obj.getId();
    ctx.objects.emplace(id, std::move(obj));
    history.recordCreated("offset", ctx.objects, id);
    created_ = id;
    LOT_LOG("offset: created object " << id << " (" << pts.size() << " points, distance " << distance_
            << ") - next object, Esc/Enter ends");
    source_ = LotGameObject::kInvalidId;
    state_ = State::PickObject;   // 다음 객체 (반복)
}

std::string OffsetTool::hint() const {
    char buf[96];
    switch (state_) {
    case State::WaitingDistance:
        std::snprintf(buf, sizeof(buf), "offset: type distance + Enter  <%g>", distance_);
        return std::string(buf) + (number_.empty() ? "" : "   [" + number_ + "]") + "  [Esc cancel]";
    case State::PickObject:
        std::snprintf(buf, sizeof(buf), "offset %g: click a line / polyline / circle / arc  [Enter/Esc ends]", distance_);
        return buf;
    case State::PickSide:
        std::snprintf(buf, sizeof(buf), "offset %g: click the side  [Esc ends]", distance_);
        return buf;
    default:
        return "";
    }
}

void OffsetTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    if (state_ != State::PickSide) return;
    const LotGameObject* src = LotGameObject::find(ctx.objects, source_);
    if (!src) return;
    // 고른 것은 노랗게, 커서 쪽 결과는 하늘색으로
    const std::vector<vec3> sp = src->worldPoints();
    const size_t sn = sp.size();
    for (size_t i = 0; i + 1 < sn + (src->closed ? 1 : 0); ++i) lines.addLine(sp[i], sp[(i + 1) % sn], kSourceColor);
    vec3 side;
    if (!sidePoint(ctx, *src, side)) return;
    std::vector<vec3> pts;
    bool closed = false;
    LotGameObject::Curve curve;
    if (!offsetShape(*src, distance_, side, workNormal_, pts, closed, curve)) return;
    const size_t n = pts.size();
    for (size_t i = 0; i + 1 < n + (closed ? 1 : 0); ++i) lines.addLine(pts[i], pts[(i + 1) % n], kGhostColor);
}
