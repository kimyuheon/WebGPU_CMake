#include "lot_array_tool.h"
#include "line_render_system.h"
#include "lot_dimension.h"
#include "lot_log.h"
#include "lot_model.h"
#include "lot_mouse_input.h"
#include "lot_picking.h"
#include "lot_sketch_tool.h"   // SketchPlane

#include <cmath>
#include <cstdio>

namespace {

constexpr int kMaxCopies = 2000;   // 네이티브 kArrayMaxInstances 와 같다
constexpr float kDegToRad = 3.14159265358979f / 180.0f;
const vec3 kGhostColor{0.4f, 0.8f, 1.0f};
const vec3 kCenterColor{1.0f, 0.5f, 0.2f};

// 객체가 차지하는 월드 점들 (경계 · 미리보기용)
std::vector<vec3> footprint(const LotGameObject& o) {
    if (o.isSketch()) return o.worldPoints();
    if (o.isDimension()) return lot_dim::outlinePoints(o);
    if (o.model) {
        const mat4 m = o.transform.mat4Transform();
        const vec3& a = o.model->boundsMin();
        const vec3& b = o.model->boundsMax();
        std::vector<vec3> c;
        for (int i = 0; i < 8; ++i) {
            c.push_back(transformPoint(m, vec3{(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z}));
        }
        return c;
    }
    return {o.transform.translation};
}

vec3 boxCenter(const std::vector<vec3>& pts) {
    if (pts.empty()) return vec3{0.0f, 0.0f, 0.0f};
    vec3 lo = pts[0], hi = pts[0];
    for (const vec3& p : pts) {
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    }
    return (lo + hi) * 0.5f;
}

}  // namespace

void ArrayTool::start(const std::set<LotGameObject::id_t>& selection, const LotCamera& camera,
                      const LotGameObject::Map& objects) {
    const SketchPlane plane = SketchPlane::fromCamera(camera);
    right_ = plane.right;
    up_ = plane.up;
    normal_ = plane.normal;
    lastJson_.clear();
    if (selection.empty()) {
        state_ = State::WaitSelect;
        LOT_LOG("array: select objects, then Enter (Esc cancels)");
        return;
    }
    openDialog(selection, objects);
}

void ArrayTool::openDialog(const std::set<LotGameObject::id_t>& selection, const LotGameObject::Map& objects) {
    sources_.assign(selection.begin(), selection.end());
    // 선택의 경계 (작업평면 축으로) - 원형 중심 기본값과 직사각형 기본 간격
    std::vector<vec3> all;
    for (LotGameObject::id_t id : sources_) {
        if (const LotGameObject* o = LotGameObject::find(objects, id)) {
            const std::vector<vec3> f = footprint(*o);
            all.insert(all.end(), f.begin(), f.end());
        }
    }
    selectionCenter_ = boxCenter(all);
    float w = 0.0f, h = 0.0f;
    if (!all.empty()) {
        float rl = dot(all[0], right_), rh = rl, ul = dot(all[0], up_), uh = ul;
        for (const vec3& p : all) {
            rl = std::fmin(rl, dot(p, right_)); rh = std::fmax(rh, dot(p, right_));
            ul = std::fmin(ul, dot(p, up_));    uh = std::fmax(uh, dot(p, up_));
        }
        w = rh - rl;
        h = uh - ul;
    }
    // 기본 간격: 선택 크기의 1.5 배 (AutoCAD). 한쪽이 0 이면 (가로선 등) 다른 쪽으로.
    if (w > 1e-9f || h > 1e-9f) {
        params_.dx = (w > 1e-9f ? w : h) * 1.5f;
        params_.dy = (h > 1e-9f ? h : w) * 1.5f;
    }
    if (params_.centerAuto) params_.center = selectionCenter_;
    state_ = State::Dialog;
    lastJson_.clear();
    LOT_LOG("array: " << sources_.size() << " objects - set the values, [생성] creates");
}

void ArrayTool::enter(const std::set<LotGameObject::id_t>& selection, LotGameObject::Map& objects,
                      EditHistory& history) {
    if (state_ == State::WaitSelect) {
        if (selection.empty()) { LOT_LOG("array: nothing selected"); return; }
        openDialog(selection, objects);
    } else if (state_ == State::Dialog) {
        // 대화상자 중 Enter = 생성 (확정 단추와 같다)
        create(objects, history);
    }
}

void ArrayTool::cancel() {
    if (state_ == State::PickCenter) {
        state_ = State::Dialog;
        LOT_LOG("array: center pick cancelled");
        return;
    }
    close();
}

void ArrayTool::close() {
    if (state_ != State::Idle) LOT_LOG("array: closed");
    state_ = State::Idle;
    sources_.clear();
    lastJson_.clear();
}

void ArrayTool::setParams(const Params& p) {
    params_ = p;
    // 네이티브 대화상자와 같은 범위
    params_.cols = std::max(1, std::min(200, params_.cols));
    params_.rows = std::max(1, std::min(200, params_.rows));
    params_.count = std::max(2, std::min(360, params_.count));
    params_.angle = std::fmax(-360.0f, std::fmin(360.0f, params_.angle));
    if (params_.centerAuto) params_.center = selectionCenter_;
}

void ArrayTool::beginPickCenter() {
    if (state_ != State::Dialog) return;
    state_ = State::PickCenter;
    LOT_LOG("array: click the polar center (Esc goes back)");
}

int ArrayTool::instanceCount() const {
    const int n = params_.polar ? params_.count - 1 : params_.cols * params_.rows - 1;
    const int perInstance = std::max<int>(1, static_cast<int>(sources_.size()));
    return std::max(0, std::min(n, kMaxCopies / perInstance));
}

void ArrayTool::placement(int i, const vec3& vc, quat& rot, vec3& pivot, vec3& offset) const {
    rot = quat::identity();
    pivot = vec3{0.0f, 0.0f, 0.0f};
    offset = vec3{0.0f, 0.0f, 0.0f};
    const int k = i + 1;   // 0 번은 원본
    if (!params_.polar) {
        const int c = k % params_.cols, r = k / params_.cols;
        offset = right_ * (static_cast<float>(c) * params_.dx) + up_ * (static_cast<float>(r) * params_.dy);
        return;
    }
    // 360 을 채우면 마지막이 첫 것에 겹치지 않게 각/개수, 아니면 각/(개수-1)
    const float span = params_.angle;
    const float step = (std::fabs(span) >= 359.99f) ? span / static_cast<float>(params_.count)
                                                    : span / static_cast<float>(params_.count - 1);
    const quat q = quat::angleAxis(static_cast<float>(k) * step * kDegToRad, normal_);
    const vec3& C = params_.center;
    if (params_.rotateItems) {
        rot = q;
        pivot = C;
    } else {
        // 회전하지 않으면 보이는 가운데만 원을 따라 옮긴다
        offset = (C + rotate(q, vc - C)) - vc;
    }
}

std::set<LotGameObject::id_t> ArrayTool::create(LotGameObject::Map& objects, EditHistory& history) {
    std::set<LotGameObject::id_t> made;
    if (state_ != State::Dialog) return made;
    const int n = instanceCount();
    for (LotGameObject::id_t sid : sources_) {
        const LotGameObject* src = LotGameObject::find(objects, sid);
        if (!src) continue;
        const vec3 vc = boxCenter(footprint(*src));
        for (int i = 0; i < n; ++i) {
            quat rot;
            vec3 pivot, offset;
            placement(i, vc, rot, pivot, offset);
            LotGameObject copy = LotGameObject::createGameObject();
            copy.color = src->color;
            copy.layer = src->layer;
            copy.linetype = src->linetype;
            copy.colorByLayer = src->colorByLayer;
            copy.model = src->model;
            copy.material = src->material;
            copy.points = src->points;
            copy.closed = src->closed;
            copy.curve = src->curve;
            copy.dim = src->dim;
            copy.text = src->text;
            copy.light = src->light;
            copy.transform = src->transform;
            copy.transform.translation = pivot + rotate(rot, src->transform.translation - pivot) + offset;
            copy.transform.rotation = normalize(rot * src->transform.rotation);
            const auto id = copy.getId();
            objects.emplace(id, std::move(copy));
            made.insert(id);
        }
    }
    if (!made.empty()) history.recordCreated("array", objects, made);
    LOT_LOG("array: created " << made.size() << " copies ("
            << (params_.polar ? "polar" : "rectangular") << ")");
    state_ = State::Idle;
    sources_.clear();
    lastJson_.clear();
    return made;
}

void ArrayTool::update(const Context& ctx) {
    if (state_ != State::PickCenter) return;
    if (!ctx.mouse.consumeLeftPress()) return;
    ctx.mouse.consumeLeftRelease();
    vec3 p;
    if (ctx.snap.valid()) {
        p = ctx.snap.point;
    } else {
        SketchPlane plane;
        plane.origin = selectionCenter_;
        plane.right = right_;
        plane.up = up_;
        plane.normal = normal_;
        const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height);
        if (!plane.intersect(ray, p)) return;
    }
    params_.center = p;
    params_.centerAuto = false;
    state_ = State::Dialog;
    LOT_LOG("array: center (" << p.x << ", " << p.y << ", " << p.z << ")");
}

void ArrayTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    if (state_ != State::Dialog && state_ != State::PickCenter) return;
    // 미리보기는 앞의 것만 (큰 배열도 프레임이 버티게)
    const int n = std::min(instanceCount(), 400);
    for (LotGameObject::id_t sid : sources_) {
        const LotGameObject* src = LotGameObject::find(ctx.objects, sid);
        if (!src) continue;
        const std::vector<vec3> pts = footprint(*src);
        const vec3 vc = boxCenter(pts);
        const bool path = src->isSketch();
        for (int i = 0; i < n; ++i) {
            quat rot;
            vec3 pivot, offset;
            placement(i, vc, rot, pivot, offset);
            auto map = [&](const vec3& p) { return pivot + rotate(rot, p - pivot) + offset; };
            if (path) {
                const size_t m = pts.size();
                const size_t segs = src->closed ? m : (m ? m - 1 : 0);
                for (size_t k = 0; k < segs; ++k) lines.addLine(map(pts[k]), map(pts[(k + 1) % m]), kGhostColor);
            } else if (pts.size() == 8) {
                const int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
                for (const auto& k : e) lines.addLine(map(pts[k[0]]), map(pts[k[1]]), kGhostColor);
            } else {
                const float s = ctx.camera.worldPerPixel(vc, ctx.height) * 6.0f;
                lines.addCross(map(vc), s, kGhostColor);
            }
        }
    }
    if (params_.polar) {
        const float s = ctx.camera.worldPerPixel(params_.center, ctx.height) * 8.0f;
        lines.addCross(params_.center, s, kCenterColor);
    }
}

std::string ArrayTool::hint() const {
    switch (state_) {
    case State::WaitSelect: return "array: select objects, then Enter  [Esc cancels]";
    case State::Dialog: return "array: set the values in the dialog - [생성] or Enter creates  [Esc closes]";
    case State::PickCenter: return "array: click the polar center  [Esc goes back]";
    default: return "";
    }
}

std::string ArrayTool::dialogJson() {
    char buf[512];
    const bool open = state_ == State::Dialog || state_ == State::PickCenter;
    std::snprintf(buf, sizeof(buf),
                  "{\"open\":%s,\"picking\":%s,\"polar\":%s,\"cols\":%d,\"rows\":%d,\"dx\":%.6g,\"dy\":%.6g,"
                  "\"count\":%d,\"angle\":%.6g,\"rotate\":%s,\"centerAuto\":%s,\"center\":[%.6g,%.6g,%.6g],"
                  "\"items\":%d,\"copies\":%d}",
                  open ? "true" : "false", state_ == State::PickCenter ? "true" : "false",
                  params_.polar ? "true" : "false", params_.cols, params_.rows, params_.dx, params_.dy,
                  params_.count, params_.angle, params_.rotateItems ? "true" : "false",
                  params_.centerAuto ? "true" : "false", params_.center.x, params_.center.y, params_.center.z,
                  static_cast<int>(sources_.size()), instanceCount() * static_cast<int>(sources_.size()));
    std::string j = buf;
    if (j == lastJson_) return std::string();
    lastJson_ = j;
    return j;
}
