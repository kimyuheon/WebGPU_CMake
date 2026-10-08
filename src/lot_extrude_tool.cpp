#include "lot_extrude_tool.h"

#include "line_render_system.h"
#include "lot_brep_shape.h"
#include "lot_feature.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "lot_picking.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
const vec3 kGhostColor{0.2f, 0.8f, 1.0f};   // 스케치 미리보기와 같은 하늘색 (네이티브 프리뷰 색)
const char* modeName(ExtrudeTool::Mode m) {
    return m == ExtrudeTool::Mode::Boss ? "boss" : m == ExtrudeTool::Mode::Pocket ? "pocket" : "extrude";
}
}  // namespace

bool ExtrudeTool::start(Mode mode, const std::set<LotGameObject::id_t>& selection, const LotGameObject::Map& objects) {
    cancel();
    mode_ = mode;
    for (const auto id : selection) {
        const LotGameObject* o = LotGameObject::find(objects, id);
        if (!o) continue;
        if (o->isSolid()) {
            if (o->brep->feature().kind == lot::LotBRepShape::FeatureKind::Extrude) solid_ = id;
            continue;
        }
        Source s{id, {}, {}, {}};
        if (lot_feature::sketchProfile(*o, s.pts, s.normal, s.center)) sources_.push_back(std::move(s));
    }
    if (mode_ == Mode::Extrude) {
        state_ = sources_.empty() ? State::PickSketch : State::Height;
        if (!sources_.empty()) {
            axis_ = sources_[0].normal;   // 여러 개면 첫 것의 평면이 기준 (드래그가 일관되게)
            center_ = sources_[0].center;
        }
        LOT_LOG("extrude: " << (sources_.empty() ? std::string("click a closed sketch")
                                                 : std::to_string(sources_.size()) + " sketch(es) - drag the height, click or type + Enter"));
        return true;
    }
    if (solid_ == LotGameObject::kInvalidId || sources_.empty()) {
        LOT_LOG(modeName(mode_) << ": select one extruded solid and closed sketch(es) first");
        sources_.clear();
        solid_ = LotGameObject::kInvalidId;
        return false;
    }
    // 높이를 재는 방향: 보스는 스케치가 가까운 캡의 바깥, 포켓은 위 캡에서 안쪽 (lot_feature 와 같은 규칙)
    const LotGameObject& solid = *LotGameObject::find(objects, solid_);
    const auto& F = solid.brep->feature();
    const mat4 m = solid.transform.mat4Transform();
    const vec3 scaledDir = transformPoint(m, F.direction) - transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
    const float dirScale = std::sqrt(dot(scaledDir, scaledDir));
    const vec3 dir = scaledDir * (1.0f / dirScale);
    const float level = dot(sources_[0].center - transformPoint(m, F.profile.front()), dir);
    const float top = F.height * dirScale;
    const bool onTop = std::fabs(level - top) <= std::fabs(level);
    axis_ = (mode_ == Mode::Pocket || !onTop) ? dir * -1.0f : dir;
    center_ = sources_[0].center;
    state_ = State::Height;
    LOT_LOG(modeName(mode_) << ": solid " << solid_ << ", " << sources_.size()
            << " sketch(es) - drag the " << (mode_ == Mode::Pocket ? "depth" : "height") << ", click or type + Enter");
    return true;
}

void ExtrudeTool::cancel() {
    if (state_ != State::Idle) LOT_LOG(modeName(mode_) << ": cancelled");
    state_ = State::Idle;
    sources_.clear();
    solid_ = LotGameObject::kInvalidId;
    number_.clear();
    haveStartY_ = false;
    height_ = 1.0f;
}

// 네이티브 computeMouseHeight: 축을 품고 카메라를 향한 평면과 커서 레이의 교점을 축에 투영.
// 축이 시선과 거의 나란하면 (위에서 본 돌출) 마우스 세로 이동을 월드 길이로.
float ExtrudeTool::mouseHeight(const Context& ctx) const {
    // 객체스냅: 그 점의 높이 ("옆 기둥 꼭대기와 같은 높이"). 단면 평면 위의 점(높이 0)은 뜻이 없어 건너뛴다 -
    // 바닥에 그린 다른 스케치의 끝점에 붙어 높이가 0 이 되던 것.
    if (ctx.snap.valid()) {
        const float h = dot(ctx.snap.point - center_, axis_);
        if (std::fabs(h) > ctx.camera.worldPerPixel(center_, ctx.height)) return h;
    }
    const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height);
    const vec3 toCamera = normalize(ctx.camera.getPosition() - center_);
    if (std::fabs(dot(toCamera, axis_)) < 0.999f) {
        const vec3 planeN = normalize(cross(axis_, cross(toCamera, axis_)));
        const float den = dot(ray.direction, planeN);
        if (std::fabs(den) > 1e-4f) {
            const float t = dot(center_ - ray.origin, planeN) / den;
            if (t > 0.0f) return dot(ray.origin + ray.direction * t - center_, axis_);
        }
    }
    const float wpp = ctx.camera.worldPerPixel(center_, ctx.height);
    return (startMouseY_ - ctx.mouse.y()) * wpp;
}

void ExtrudeTool::update(const Context& ctx, EditHistory& history, lot_web_device& device) {
    if (!isActive()) return;
    if (state_ == State::PickSketch) {
        if (!ctx.mouse.consumeLeftPress()) { ctx.mouse.consumeLeftRelease(); return; }
        ctx.mouse.consumeLeftRelease();
        float dist = 0.0f;
        const auto id = lot_pick::pickSketch(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height,
                                             8.0f, ctx.objects, dist);
        const LotGameObject* o = LotGameObject::find(ctx.objects, id);
        Source s{id, {}, {}, {}};
        if (!o || !lot_feature::sketchProfile(*o, s.pts, s.normal, s.center)) {
            LOT_LOG("extrude: not a closed sketch there");
            return;
        }
        axis_ = s.normal;
        center_ = s.center;
        sources_.push_back(std::move(s));
        state_ = State::Height;
        haveStartY_ = false;
        LOT_LOG("extrude: sketch " << id << " - drag the height, click or type + Enter");
        return;
    }
    if (!haveStartY_) {
        startMouseY_ = ctx.mouse.y();
        haveStartY_ = true;
    }
    float h = mouseHeight(ctx);
    // 0 높이 방지 - 마우스로 정할 때만, 화면 1픽셀 만큼 (친 값에는 걸지 않는다, 네이티브와 같다)
    const float minH = ctx.camera.worldPerPixel(center_, ctx.height);
    if (std::fabs(h) < minH) h = (h >= 0.0f ? 1.0f : -1.0f) * minH;
    height_ = h;
    if (!ctx.mouse.consumeLeftPress()) { ctx.mouse.consumeLeftRelease(); return; }
    ctx.mouse.consumeLeftRelease();
    commit(ctx, height_, history, device);
}

void ExtrudeTool::finish(const Context& ctx, EditHistory& history, lot_web_device& device) {
    if (state_ != State::Height) {
        cancel();
        return;
    }
    float h = height_;
    if (!number_.empty()) {
        char* end = nullptr;
        const float v = std::strtof(number_.c_str(), &end);
        if (end == number_.c_str() || !std::isfinite(v) || v == 0.0f) {
            LOT_LOG(modeName(mode_) << ": \"" << number_ << "\" is not a usable value");
            number_.clear();
            return;
        }
        h = v;
    }
    commit(ctx, h, history, device);
}

void ExtrudeTool::commit(const Context& ctx, float h, EditHistory& history, lot_web_device& device) {
    std::string why;
    if (mode_ == Mode::Extrude) {
        std::set<LotGameObject::id_t> made;
        for (const Source& s : sources_) {
            const auto id = lot_feature::extrude(ctx.objects, s.id, h, device, why);
            if (id != LotGameObject::kInvalidId) made.insert(id);
            else LOT_LOG("extrude: sketch " << s.id << " - " << why);
        }
        if (made.empty()) {   // 아무것도 못 만들었으면 도구는 그대로 - 다시 끌거나 값을 친다
            number_.clear();
            return;
        }
        history.recordCreated("extrude", ctx.objects, made);
        result_ = made;
        LOT_LOG("extrude: " << made.size() << " solid(s), height " << h);
    } else {
        if (!(h > 0.0f)) {
            LOT_LOG(modeName(mode_) << ": the " << (mode_ == Mode::Pocket ? "depth" : "height")
                    << " must point away from the solid (got " << h << ")");
            number_.clear();
            return;
        }
        int done = 0;
        for (const Source& s : sources_) {
            const bool ok = mode_ == Mode::Boss
                ? lot_feature::boss(ctx.objects, solid_, s.id, h, device, history, why)
                : lot_feature::cut(ctx.objects, solid_, s.id, h, false, device, history, why);
            if (ok) ++done;
            else LOT_LOG(modeName(mode_) << ": sketch " << s.id << " - " << why);
        }
        if (done == 0) {   // 아무것도 못 했으면 도구는 그대로
            number_.clear();
            return;
        }
        result_ = {solid_};
    }
    state_ = State::Idle;
    sources_.clear();
    solid_ = LotGameObject::kInvalidId;
    number_.clear();
    haveStartY_ = false;
}

std::string ExtrudeTool::hint() const {
    if (state_ == State::PickSketch) return "extrude: click a closed sketch  [Esc cancel]";
    if (state_ != State::Height) return "";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s: %s %.4g - click, or type a value + Enter", modeName(mode_),
                  mode_ == Mode::Pocket ? "depth" : "height", height_);
    return std::string(buf) + (number_.empty() ? "" : "   [" + number_ + "]") + "  [Esc cancel]";
}

void ExtrudeTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    (void)ctx;
    if (state_ != State::Height) return;
    float h = height_;
    if (!number_.empty()) {
        const float v = std::strtof(number_.c_str(), nullptr);
        if (std::isfinite(v) && v != 0.0f) h = v;
    }
    // 바닥 고리 · 윗 고리 · 세로 모서리 (원은 세로를 몇 개만)
    for (const Source& s : sources_) {
        const vec3 off = (mode_ == Mode::Extrude ? s.normal : axis_) * h;
        const size_t n = s.pts.size();
        const size_t step = n > 16 ? n / 8 : 1;
        for (size_t i = 0; i < n; ++i) {
            const vec3& a = s.pts[i];
            const vec3& b = s.pts[(i + 1) % n];
            lines.addLine(a, b, kGhostColor);
            lines.addLine(a + off, b + off, kGhostColor);
            if (i % step == 0) lines.addLine(a, a + off, kGhostColor);
        }
    }
}
