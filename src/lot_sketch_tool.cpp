#include "lot_sketch_tool.h"
#include "line_render_system.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "polyline_render_system.h"

#include <cmath>

namespace {

const vec3 kLineColor{0.92f, 0.92f, 0.92f};
const vec3 kRectangleColor{0.75f, 0.85f, 1.0f};
const vec3 kPolylineColor{0.8f, 1.0f, 0.8f};
const vec3 kPreviewColor{0.55f, 0.8f, 1.0f};
const vec3 kCursorColor{1.0f, 1.0f, 1.0f};

// 두 점이 사실상 같은 자리인가 (더블 클릭, 같은 스냅 점 두 번)
bool samePoint(const vec3& a, const vec3& b) {
    const vec3 d = a - b;
    return dot(d, d) < 1e-8f;
}

}  // namespace

// ---------------------------------------------------------------- SketchPlane

SketchPlane SketchPlane::fromCamera(const LotCamera& camera) {
    // 카메라가 보는 방향에서 가장 큰 성분의 축이 법선 = 화면에 가장 정면으로 놓인 평면.
    const vec3 f = camera.getForward();
    const float ax = std::fabs(f.x), ay = std::fabs(f.y), az = std::fabs(f.z);

    SketchPlane p;
    if (ay >= ax && ay >= az) {
        // 위/아래에서 본다 -> 바닥 (XZ). 화면 위쪽이 +Z 가 되도록 up = +Z 가 아니라
        // 카메라 up 을 그대로 쓰는 편이 사각형 도구에서 직관적이지만, 축 정렬을
        // 유지해야 스냅/치수 계산이 단순하므로 월드 축으로 고정한다.
        p.right = vec3{1.0f, 0.0f, 0.0f};
        p.up = vec3{0.0f, 0.0f, 1.0f};
        p.normal = vec3{0.0f, 1.0f, 0.0f};
        p.name = "XZ (floor)";
    } else if (ax >= az) {
        // 옆에서 본다 -> YZ. 위는 -Y.
        p.right = vec3{0.0f, 0.0f, 1.0f};
        p.up = vec3{0.0f, -1.0f, 0.0f};
        p.normal = vec3{1.0f, 0.0f, 0.0f};
        p.name = "YZ (side)";
    } else {
        // 정면 -> XY. 위는 -Y.
        p.right = vec3{1.0f, 0.0f, 0.0f};
        p.up = vec3{0.0f, -1.0f, 0.0f};
        p.normal = vec3{0.0f, 0.0f, 1.0f};
        p.name = "XY (front)";
    }
    return p;
}

bool SketchPlane::intersect(const lot_pick::Ray& ray, vec3& out) const {
    const float denom = dot(ray.direction, normal);
    if (std::fabs(denom) < 1e-6f) return false;
    const float t = dot(origin - ray.origin, normal) / denom;
    if (t < 0.0f) return false;  // 평면이 카메라 뒤
    out = ray.origin + ray.direction * t;
    return true;
}

// ---------------------------------------------------------------- SketchTool

LotGameObject::id_t SketchTool::commit(const std::vector<vec3>& worldPoints, bool closed,
                                       const vec3& color, LotGameObject::Map& objects) {
    if (worldPoints.size() < 2) return LotGameObject::kInvalidId;

    vec3 centroid{0.0f, 0.0f, 0.0f};
    for (const vec3& p : worldPoints) centroid = centroid + p;
    centroid = centroid * (1.0f / static_cast<float>(worldPoints.size()));

    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = centroid;
    obj.color = color;
    obj.closed = closed;
    obj.points.reserve(worldPoints.size());
    for (const vec3& p : worldPoints) obj.points.push_back(p - centroid);

    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
    committedId_ = id;
    LOT_LOG("sketch: " << name() << " committed as object " << id << " ("
            << worldPoints.size() << " points" << (closed ? ", closed)" : ")"));
    return id;
}

// ---------------------------------------------------------------- LineTool

void LineTool::onPoint(const vec3& p, const SketchPlane&, LotGameObject::Map& objects) {
    if (points_.empty()) {
        points_.push_back(p);
        return;
    }
    if (samePoint(points_.back(), p)) return;  // 길이 0 선분은 만들지 않는다
    commit({points_.back(), p}, false, kLineColor, objects);
    points_ = {p};  // 끝점에서 다음 선분이 이어진다
}

bool LineTool::onFinish(LotGameObject::Map&) {
    // 선은 클릭마다 확정되므로 Enter 는 '이어 그리기 끝'일 뿐이다
    points_.clear();
    return true;
}

void LineTool::preview(const vec3& cursor, const SketchPlane&,
                       std::vector<vec3>& out, bool& closed) const {
    closed = false;
    out.clear();
    if (points_.empty()) return;
    out = {points_.back(), cursor};
}

// ---------------------------------------------------------------- RectangleTool

std::vector<vec3> RectangleTool::corners(const vec3& p0, const vec3& p1, const SketchPlane& plane) {
    const vec3 d = p1 - p0;
    const vec3 alongRight = plane.right * dot(d, plane.right);
    const vec3 alongUp = plane.up * dot(d, plane.up);
    return {p0, p0 + alongRight, p0 + alongRight + alongUp, p0 + alongUp};
}

void RectangleTool::onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) {
    if (points_.empty()) {
        points_.push_back(p);
        return;
    }
    const vec3 d = p - points_.front();
    if (std::fabs(dot(d, plane.right)) < 1e-5f || std::fabs(dot(d, plane.up)) < 1e-5f) {
        return;  // 넓이 0 - 대각선이 축과 나란하면 사각형이 아니다
    }
    commit(corners(points_.front(), p, plane), true, kRectangleColor, objects);
    points_.clear();  // 사각형은 하나 그리면 끝. 다음 것은 다시 첫 점부터.
}

bool RectangleTool::onFinish(LotGameObject::Map&) {
    points_.clear();
    return true;
}

void RectangleTool::preview(const vec3& cursor, const SketchPlane& plane,
                            std::vector<vec3>& out, bool& closed) const {
    out.clear();
    closed = true;
    if (points_.empty()) return;
    out = corners(points_.front(), cursor, plane);
}

// ---------------------------------------------------------------- PolylineTool

void PolylineTool::onPoint(const vec3& p, const SketchPlane&, LotGameObject::Map& objects) {
    if (points_.size() >= 3) {
        const vec3 d = p - points_.front();
        if (dot(d, d) <= closeRadius * closeRadius) {
            commit(points_, true, kPolylineColor, objects);
            points_.clear();
            return;
        }
    }
    if (!points_.empty() && samePoint(points_.back(), p)) return;
    points_.push_back(p);
}

bool PolylineTool::onFinish(LotGameObject::Map& objects) {
    if (points_.size() >= 2) commit(points_, false, kPolylineColor, objects);
    points_.clear();
    return true;
}

void PolylineTool::preview(const vec3& cursor, const SketchPlane&,
                           std::vector<vec3>& out, bool& closed) const {
    closed = false;
    out = points_;
    if (out.empty()) return;
    // 첫 점 근처면 닫힐 것을 미리 보여준다
    if (out.size() >= 3) {
        const vec3 d = cursor - out.front();
        if (dot(d, d) <= closeRadius * closeRadius) {
            closed = true;
            return;
        }
    }
    out.push_back(cursor);
}

// ---------------------------------------------------------------- SketchController

SketchController::SketchController()
    : line_(std::make_unique<LineTool>()),
      rectangle_(std::make_unique<RectangleTool>()),
      polyline_(std::make_unique<PolylineTool>()) {}

void SketchController::start(Kind kind, const LotCamera& camera) {
    cancel();
    switch (kind) {
    case Kind::Line:      active_ = line_.get(); break;
    case Kind::Rectangle: active_ = rectangle_.get(); break;
    case Kind::Polyline:  active_ = polyline_.get(); break;
    }
    plane_ = SketchPlane::fromCamera(camera);
    active_->begin();
    LOT_LOG("sketch: " << active_->name() << " on plane " << plane_.name
            << " - click points, Enter to finish, Esc to cancel");
}

void SketchController::cancel() {
    if (!active_) return;
    LOT_LOG("sketch: " << active_->name() << " cancelled");
    active_->cancel();
    active_ = nullptr;
}

void SketchController::finish(LotGameObject::Map& objects) {
    if (!active_) return;
    active_->onFinish(objects);
    const auto id = active_->consumeCommittedId();
    if (id != LotGameObject::kInvalidId) lastCommitted_ = id;
    LOT_LOG("sketch: " << active_->name() << " finished");
    active_ = nullptr;
}

bool SketchController::cursorPoint(const Context& ctx, vec3& out) const {
    if (ctx.snap.valid()) {
        out = ctx.snap.point;  // 스냅이 잡혔으면 평면 밖이라도 그 점 (AutoCAD 의 OSNAPZ=0)
        return true;
    }
    const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(),
                                                    ctx.width, ctx.height);
    return plane_.intersect(ray, out);
}

void SketchController::update(const Context& ctx) {
    if (!active_) return;

    // 폴리라인 닫기 반경을 지금 커서 깊이의 픽셀 크기로 환산해 둔다
    vec3 cursor;
    const bool haveCursor = cursorPoint(ctx, cursor);
    if (haveCursor) {
        polyline_->closeRadius = ctx.camera.worldPerPixel(cursor, ctx.height) * closeRadiusPx;
    }

    if (ctx.mouse.consumeLeftPress()) {
        if (haveCursor) {
            active_->onPoint(cursor, plane_, ctx.objects);
            const auto id = active_->consumeCommittedId();
            if (id != LotGameObject::kInvalidId) {
                lastCommitted_ = id;
                if (active_->endsAfterCommit()) active_ = nullptr;
            }
        } else {
            LOT_LOG("sketch: cursor misses the " << plane_.name << " plane - rotate the view");
        }
    }
    ctx.mouse.consumeLeftRelease();  // 뗌은 쓰지 않지만 플래그는 비워둔다
}

void SketchController::drawPreview(PolylineRenderSystem& polylines, LineRenderSystem& lines,
                                   const Context& ctx) const {
    if (!active_) return;

    vec3 cursor;
    if (!cursorPoint(ctx, cursor)) return;

    // 커서 자리 십자 - 어느 평면에 찍히는지 보인다
    const float s = ctx.camera.worldPerPixel(cursor, ctx.height) * 6.0f;
    lines.addLine(cursor - plane_.right * s, cursor + plane_.right * s, kCursorColor);
    lines.addLine(cursor - plane_.up * s, cursor + plane_.up * s, kCursorColor);

    std::vector<vec3> pts;
    bool closed = false;
    active_->preview(cursor, plane_, pts, closed);
    if (pts.size() >= 2) polylines.addPolyline(pts, kPreviewColor, closed);
}

int SketchController::activeKind() const {
    if (active_ == line_.get()) return static_cast<int>(Kind::Line);
    if (active_ == rectangle_.get()) return static_cast<int>(Kind::Rectangle);
    if (active_ == polyline_.get()) return static_cast<int>(Kind::Polyline);
    return -1;
}

std::string SketchController::hint() const {
    if (!active_) return "";
    std::string s = active_->name();
    s += " on ";
    s += plane_.name;
    s += ": ";
    if (active_ == line_.get()) {
        s += active_->hasPoints() ? "click next point (Enter/Esc to stop)"
                                  : "click first point";
    } else if (active_ == rectangle_.get()) {
        s += active_->hasPoints() ? "click opposite corner" : "click first corner";
    } else {
        const size_t n = active_->points().size();
        if (n == 0) s += "click first point";
        else if (n < 3) s += "click next point (Enter = open)";
        else s += "click next point, first point = close, Enter = open";
    }
    s += "  [Esc cancel]";
    return s;
}

LotGameObject::id_t SketchController::consumeCommittedId() {
    const auto id = lastCommitted_;
    lastCommitted_ = LotGameObject::kInvalidId;
    return id;
}
