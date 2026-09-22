#include "lot_sketch_tool.h"
#include "line_render_system.h"
#include "lot_dimension.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "polyline_render_system.h"
#include "text_render_system.h"

#include <cmath>

namespace {

const vec3 kLineColor{0.92f, 0.92f, 0.92f};
const vec3 kRectangleColor{0.75f, 0.85f, 1.0f};
const vec3 kPolylineColor{0.8f, 1.0f, 0.8f};
const vec3 kCircleColor{1.0f, 0.85f, 0.7f};
const vec3 kArcColor{1.0f, 0.75f, 0.85f};
const vec3 kPolygonColor{0.85f, 0.8f, 1.0f};
const vec3 kDimensionColor{0.95f, 0.85f, 0.55f};
const vec3 kTextColor{0.95f, 0.95f, 0.9f};
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
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

    // 축은 카메라 up 이 아니라 월드 축으로 고정한다 - 스냅/치수 계산이 단순하고,
    // 사각형의 변이 항상 축에 나란하다. (Vulkan 쪽 getPlaneVectors 와 같은 값)
    SketchPlane p;
    if (az >= ax && az >= ay) {
        // 위/아래에서 본다 -> 바닥 (XY). 화면 위 = +Y.
        p.right = vec3{1.0f, 0.0f, 0.0f};
        p.up = vec3{0.0f, 1.0f, 0.0f};
        p.normal = vec3{0.0f, 0.0f, 1.0f};
        p.name = "XY (floor)";
    } else if (ax >= ay) {
        // 옆에서 본다 -> YZ. 위는 +Z.
        p.right = vec3{0.0f, 1.0f, 0.0f};
        p.up = vec3{0.0f, 0.0f, 1.0f};
        p.normal = vec3{1.0f, 0.0f, 0.0f};
        p.name = "YZ (side)";
    } else {
        // 정면 -> XZ. 위는 +Z.
        p.right = vec3{1.0f, 0.0f, 0.0f};
        p.up = vec3{0.0f, 0.0f, 1.0f};
        p.normal = vec3{0.0f, 1.0f, 0.0f};
        p.name = "XZ (front)";
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

// ---------------------------------------------------------------- tessellation

std::vector<vec3> tessellateArc(const vec3& center, float radius, const vec3& right,
                                const vec3& up, float start, float end, bool includeEnd) {
    const float sweep = end - start;
    int segments = static_cast<int>(std::fabs(sweep) / kTwoPi * 64.0f + 0.5f);
    if (segments < 8) segments = 8;
    std::vector<vec3> pts;
    pts.reserve(static_cast<size_t>(segments) + 1);
    const int last = includeEnd ? segments : segments - 1;
    for (int i = 0; i <= last; ++i) {
        const float t = start + sweep * static_cast<float>(i) / static_cast<float>(segments);
        pts.push_back(center + right * (radius * std::cos(t)) + up * (radius * std::sin(t)));
    }
    return pts;
}

// ---------------------------------------------------------------- SketchTool

LotGameObject::id_t SketchTool::commit(const std::vector<vec3>& worldPoints, bool closed,
                                       const vec3& color, LotGameObject::Map& objects,
                                       const LotGameObject::Curve* curve) {
    if (worldPoints.size() < 2) return LotGameObject::kInvalidId;

    vec3 origin{0.0f, 0.0f, 0.0f};
    if (curve) {
        origin = curve->center;  // 원/호는 중심이 기준점 - 회전/축척 기즈모가 중심에서 돈다
    } else {
        for (const vec3& p : worldPoints) origin = origin + p;
        origin = origin * (1.0f / static_cast<float>(worldPoints.size()));
    }

    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = origin;
    obj.color = color;
    obj.closed = closed;
    obj.points.reserve(worldPoints.size());
    for (const vec3& p : worldPoints) obj.points.push_back(p - origin);
    if (curve) {
        obj.curve = *curve;
        obj.curve.center = vec3{0.0f, 0.0f, 0.0f};  // 로컬
    }

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

// ---------------------------------------------------------------- CircleTool

void CircleTool::onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) {
    if (points_.empty()) {
        points_.push_back(p);
        return;
    }
    const vec3 d = p - points_.front();
    const float r = std::sqrt(dot(d, d));
    if (r < 1e-5f) return;
    LotGameObject::Curve c;
    c.kind = LotGameObject::Curve::Kind::Circle;
    c.center = points_.front();
    c.radius = r;
    c.right = plane.right;
    c.up = plane.up;
    c.start = 0.0f;
    c.end = kTwoPi;
    commit(tessellateArc(c.center, r, plane.right, plane.up, 0.0f, kTwoPi, false), true,
           kCircleColor, objects, &c);
    points_.clear();
}

bool CircleTool::onFinish(LotGameObject::Map&) {
    points_.clear();
    return true;
}

void CircleTool::preview(const vec3& cursor, const SketchPlane& plane,
                         std::vector<vec3>& out, bool& closed) const {
    out.clear();
    closed = true;
    if (points_.empty()) return;
    const vec3 d = cursor - points_.front();
    const float r = std::sqrt(dot(d, d));
    if (r < 1e-5f) return;
    out = tessellateArc(points_.front(), r, plane.right, plane.up, 0.0f, kTwoPi, false);
    // 반지름 선도 같이 - 어디를 잡았는지 보인다 (닫힌 원에 끼우면 모양이 깨지므로 따로)
}

// ---------------------------------------------------------------- ArcTool

bool ArcTool::solve(const vec3& a, const vec3& b, const vec3& c, const SketchPlane& plane,
                    LotGameObject::Curve& out) {
    // 평면 2D 로 내려서 외심을 구한다. (평면 밖 성분 - 스냅으로 잡힌 점 - 은 버린다)
    auto to2 = [&](const vec3& p, float& x, float& y) {
        const vec3 d = p - a;
        x = dot(d, plane.right);
        y = dot(d, plane.up);
    };
    float bx, by, cx, cy;
    to2(b, bx, by);
    to2(c, cx, cy);
    // a 는 (0, 0). 외심 (ux, uy): 표준 공식.
    const float dd = 2.0f * (bx * cy - by * cx);
    if (std::fabs(dd) < 1e-9f) return false;  // 한 직선
    const float b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const float ux = (cy * b2 - by * c2) / dd;
    const float uy = (bx * c2 - cx * b2) / dd;
    const float r = std::sqrt(ux * ux + uy * uy);
    if (r < 1e-6f) return false;

    // 각도: 중심에서 본 a, b, c. a -> c 로 가되 b 를 지나는 방향을 고른다.
    auto angleOf = [&](float x, float y) { return std::atan2(y - uy, x - ux); };
    const float ta = angleOf(0.0f, 0.0f);
    const float tb = angleOf(bx, by);
    const float tc = angleOf(cx, cy);
    auto ccw = [](float from, float to) {  // from 에서 to 까지 반시계 누적각 [0, 2π)
        float d = to - from;
        while (d < 0.0f) d += kTwoPi;
        while (d >= kTwoPi) d -= kTwoPi;
        return d;
    };
    const float sweepCcw = ccw(ta, tc);
    const bool bOnCcw = ccw(ta, tb) <= sweepCcw;
    const float sweep = bOnCcw ? sweepCcw : -(kTwoPi - sweepCcw);

    out.kind = LotGameObject::Curve::Kind::Arc;
    out.center = a + plane.right * ux + plane.up * uy;
    out.radius = r;
    out.right = plane.right;
    out.up = plane.up;
    out.start = ta;
    out.end = ta + sweep;
    return true;
}

void ArcTool::onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) {
    if (!points_.empty() && samePoint(points_.back(), p)) return;
    points_.push_back(p);
    if (points_.size() < 3) return;
    LotGameObject::Curve c;
    if (ArcTool::solve(points_[0], points_[1], points_[2], plane, c)) {
        commit(tessellateArc(c.center, c.radius, c.right, c.up, c.start, c.end), false,
               kArcColor, objects, &c);
    } else {
        LOT_LOG("sketch: arc - the three points are collinear, try again");
    }
    points_.clear();
}

bool ArcTool::onFinish(LotGameObject::Map&) {
    points_.clear();
    return true;
}

void ArcTool::preview(const vec3& cursor, const SketchPlane& plane,
                      std::vector<vec3>& out, bool& closed) const {
    out.clear();
    closed = false;
    if (points_.empty()) return;
    if (points_.size() == 1) {
        out = {points_[0], cursor};  // 아직 직선 - 두 번째 점을 기다린다
        return;
    }
    LotGameObject::Curve c;
    if (ArcTool::solve(points_[0], points_[1], cursor, plane, c)) {
        out = tessellateArc(c.center, c.radius, c.right, c.up, c.start, c.end);
    } else {
        out = {points_[0], points_[1], cursor};
    }
}

// ---------------------------------------------------------------- PolygonTool

std::vector<vec3> PolygonTool::vertices(const vec3& center, const vec3& vertex,
                                        const SketchPlane& plane, int sides) {
    const vec3 d = vertex - center;
    const float x = dot(d, plane.right), y = dot(d, plane.up);
    const float r = std::sqrt(x * x + y * y);
    const float t0 = std::atan2(y, x);  // 찍은 꼭짓점이 첫 꼭짓점
    std::vector<vec3> pts;
    pts.reserve(static_cast<size_t>(sides));
    for (int i = 0; i < sides; ++i) {
        const float t = t0 + kTwoPi * static_cast<float>(i) / static_cast<float>(sides);
        pts.push_back(center + plane.right * (r * std::cos(t)) + plane.up * (r * std::sin(t)));
    }
    return pts;
}

void PolygonTool::onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) {
    if (points_.empty()) {
        points_.push_back(p);
        return;
    }
    const vec3 d = p - points_.front();
    if (dot(d, d) < 1e-10f) return;
    commit(vertices(points_.front(), p, plane, sides), true, kPolygonColor, objects);
    points_.clear();
}

bool PolygonTool::onFinish(LotGameObject::Map&) {
    points_.clear();
    return true;
}

void PolygonTool::preview(const vec3& cursor, const SketchPlane& plane,
                          std::vector<vec3>& out, bool& closed) const {
    out.clear();
    closed = true;
    if (points_.empty()) return;
    const vec3 d = cursor - points_.front();
    if (dot(d, d) < 1e-10f) return;
    out = vertices(points_.front(), cursor, plane, sides);
}

// ---------------------------------------------------------------- DimensionTool

LotGameObject::Dim DimensionTool::makeDim(const vec3& p1, const vec3& p2, const vec3& dimLine,
                                          const SketchPlane& plane, float textHeight,
                                          float arrowSize, vec3& originOut) {
    originOut = (p1 + p2) * 0.5f;
    LotGameObject::Dim d;
    d.valid = true;
    d.p1 = p1 - originOut;
    d.p2 = p2 - originOut;
    d.dimLine = dimLine - originOut;
    d.normal = plane.normal;
    d.textHeight = textHeight;
    d.arrowSize = arrowSize;
    return d;
}

void DimensionTool::onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) {
    if (!points_.empty() && samePoint(points_.back(), p)) return;
    points_.push_back(p);
    if (points_.size() < 3) return;

    vec3 origin;
    LotGameObject::Dim d = makeDim(points_[0], points_[1], points_[2], plane, textHeight, arrowSize, origin);
    d.precision = precision;

    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = origin;
    obj.color = kDimensionColor;
    obj.dim = d;
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
    committedId_ = id;
    LOT_LOG("sketch: dimension committed as object " << id << " (value "
            << lot_dim::formatValue(std::sqrt(dot(points_[1] - points_[0], points_[1] - points_[0])),
                                    precision) << ")");
    points_.clear();
}

bool DimensionTool::onFinish(LotGameObject::Map&) {
    points_.clear();
    return true;
}

void DimensionTool::preview(const vec3& cursor, const SketchPlane&,
                            std::vector<vec3>& out, bool& closed) const {
    // 측정점 둘을 잡기 전까지는 고무줄 하나. 그 뒤는 previewExtra 가 치수 전체를 그린다.
    out.clear();
    closed = false;
    if (points_.size() == 1) out = {points_[0], cursor};
}

void DimensionTool::previewExtra(const vec3& cursor, const SketchPlane& plane, const LotCamera& camera,
                                 LineRenderSystem& lines, TextRenderSystem& text) const {
    if (points_.size() < 2) return;
    vec3 origin;
    LotGameObject::Dim d = makeDim(points_[0], points_[1], cursor, plane, textHeight, arrowSize, origin);
    d.precision = precision;
    mat4 m = mat4::identity();
    m.m[3][0] = origin.x; m.m[3][1] = origin.y; m.m[3][2] = origin.z;
    const lot_dim::Geometry g = lot_dim::build(d, m, &camera);
    for (const auto& s : g.segments) lines.addLine(s.first, s.second, kPreviewColor);
    if (!g.text.empty()) text.addText(g.text, g.textOrigin, g.textRight, g.textUp, g.textHeight, kPreviewColor, 1);
}

// ---------------------------------------------------------------- TextTool

void TextTool::onPoint(const vec3& p, const SketchPlane&, LotGameObject::Map&) {
    if (waiting_) return;  // 입력창이 열린 동안의 클릭은 무시
    points_ = {p};
    inputRequested_ = true;
    waiting_ = true;
}

void TextTool::submit(const std::string& content, const SketchPlane& plane, LotGameObject::Map& objects) {
    waiting_ = false;
    if (points_.empty() || content.empty()) {
        points_.clear();
        return;
    }
    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = points_.front();
    obj.color = kTextColor;
    obj.text.valid = true;
    obj.text.content = content;
    obj.text.height = height;
    obj.text.right = plane.right;
    obj.text.up = plane.up;
    obj.text.hAlign = 0;
    obj.text.vAlign = 0;
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
    committedId_ = id;
    LOT_LOG("sketch: text committed as object " << id << " (\"" << content << "\")");
    points_.clear();
}

bool TextTool::onFinish(LotGameObject::Map&) {
    points_.clear();
    waiting_ = false;
    return true;
}

void TextTool::preview(const vec3&, const SketchPlane&, std::vector<vec3>& out, bool& closed) const {
    out.clear();
    closed = false;
}

void TextTool::previewExtra(const vec3& cursor, const SketchPlane& plane, const LotCamera&,
                            LineRenderSystem& lines, TextRenderSystem& text) const {
    // 기준점(또는 커서)에 글자 높이만큼의 밑줄 + 자리표시 글자
    const vec3 at = points_.empty() ? cursor : points_.front();
    lines.addLine(at, at + plane.right * (height * 2.0f), kPreviewColor);
    lines.addLine(at, at + plane.up * height, kPreviewColor);
    if (waiting_) text.addText("...", at, plane.right, plane.up, height, kPreviewColor, 0, 0);
}

// ---------------------------------------------------------------- SketchController

SketchController::SketchController()
    : line_(std::make_unique<LineTool>()),
      rectangle_(std::make_unique<RectangleTool>()),
      polyline_(std::make_unique<PolylineTool>()),
      circle_(std::make_unique<CircleTool>()),
      arc_(std::make_unique<ArcTool>()),
      polygon_(std::make_unique<PolygonTool>()),
      dimension_(std::make_unique<DimensionTool>()),
      textTool_(std::make_unique<TextTool>()) {}

void SketchController::setDimensionStyle(float textHeight, float arrowSize) {
    dimension_->textHeight = textHeight;
    dimension_->arrowSize = arrowSize;
}

void SketchController::setTextHeight(float height) { textTool_->height = height; }

bool SketchController::consumeTextInputRequest() {
    return active_ == textTool_.get() && textTool_->consumeInputRequest();
}

bool SketchController::waitingForTextInput() const {
    return active_ == textTool_.get() && textTool_->waitingForInput();
}

void SketchController::submitText(const std::string& content, LotGameObject::Map& objects) {
    if (active_ != textTool_.get()) return;
    textTool_->submit(content, plane_, objects);
    const auto id = textTool_->consumeCommittedId();
    if (id != LotGameObject::kInvalidId) lastCommitted_ = id;
    active_ = nullptr;  // 문자는 하나 놓으면 끝
}

void SketchController::changePolygonSides(int delta) {
    int n = polygon_->sides + delta;
    if (n < 3) n = 3;
    if (n > 32) n = 32;
    polygon_->sides = n;
    LOT_LOG("sketch: polygon sides = " << n);
}

int SketchController::polygonSides() const { return polygon_->sides; }

void SketchController::start(Kind kind, const LotCamera& camera) {
    cancel();
    switch (kind) {
    case Kind::Line:      active_ = line_.get(); break;
    case Kind::Rectangle: active_ = rectangle_.get(); break;
    case Kind::Polyline:  active_ = polyline_.get(); break;
    case Kind::Circle:    active_ = circle_.get(); break;
    case Kind::Arc:       active_ = arc_.get(); break;
    case Kind::Polygon:   active_ = polygon_.get(); break;
    case Kind::Dimension: active_ = dimension_.get(); break;
    case Kind::Text:      active_ = textTool_.get(); break;
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

const vec3* SketchController::referencePoint() const {
    if (!active_ || !active_->hasPoints()) return nullptr;
    return &active_->points().back();
}

// 직교 트랙킹: 기준점에서 커서까지의 변위를 평면의 두 축 중 더 많이 움직인 쪽으로만.
// 축은 매 프레임 다시 고르므로 드래그하다 방향을 틀면 자연스럽게 바뀐다.
vec3 orthoConstrain(const vec3& from, const vec3& cursor, const SketchPlane& plane) {
    const vec3 d = cursor - from;
    const float dr = dot(d, plane.right), du = dot(d, plane.up);
    return (std::fabs(dr) >= std::fabs(du)) ? from + plane.right * dr : from + plane.up * du;
}

bool SketchController::cursorPoint(const Context& ctx, vec3& out) const {
    if (ctx.snap.valid()) {
        out = ctx.snap.point;  // 스냅이 잡혔으면 평면 밖이라도 그 점 (AutoCAD 의 OSNAPZ=0)
        return true;
    }
    const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(),
                                                    ctx.width, ctx.height);
    if (!plane_.intersect(ray, out)) return false;
    if (orthoTracking) {
        if (const vec3* ref = referencePoint()) out = orthoConstrain(*ref, out, plane_);
    }
    return true;
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
                                   TextRenderSystem& text, const Context& ctx) const {
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
    active_->previewExtra(cursor, plane_, ctx.camera, lines, text);
}

int SketchController::activeKind() const {
    if (active_ == line_.get()) return static_cast<int>(Kind::Line);
    if (active_ == rectangle_.get()) return static_cast<int>(Kind::Rectangle);
    if (active_ == polyline_.get()) return static_cast<int>(Kind::Polyline);
    if (active_ == circle_.get()) return static_cast<int>(Kind::Circle);
    if (active_ == arc_.get()) return static_cast<int>(Kind::Arc);
    if (active_ == polygon_.get()) return static_cast<int>(Kind::Polygon);
    if (active_ == dimension_.get()) return static_cast<int>(Kind::Dimension);
    if (active_ == textTool_.get()) return static_cast<int>(Kind::Text);
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
    } else if (active_ == circle_.get()) {
        s += active_->hasPoints() ? "click a point on the circle (radius)" : "click center";
    } else if (active_ == arc_.get()) {
        const size_t n = active_->points().size();
        s += n == 0 ? "click start point" : (n == 1 ? "click a point on the arc" : "click end point");
    } else if (active_ == polygon_.get()) {
        s += std::to_string(polygon_->sides) + " sides ([ / ] to change): ";
        s += active_->hasPoints() ? "click a vertex (radius)" : "click center";
    } else if (active_ == dimension_.get()) {
        const size_t n = active_->points().size();
        s += n == 0 ? "click first measure point" : (n == 1 ? "click second measure point"
                                                             : "click where the dimension line goes");
    } else if (active_ == textTool_.get()) {
        s += textTool_->waitingForInput() ? "type the text, Enter to place" : "click the text start point";
    } else {
        const size_t n = active_->points().size();
        if (n == 0) s += "click first point";
        else if (n < 3) s += "click next point (Enter = open)";
        else s += "click next point, first point = close, Enter = open";
    }
    s += "  [Esc cancel]";
    if (orthoTracking) s += "  ORTHO";
    return s;
}

LotGameObject::id_t SketchController::consumeCommittedId() {
    const auto id = lastCommitted_;
    lastCommitted_ = LotGameObject::kInvalidId;
    return id;
}
