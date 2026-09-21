#include "lot_transform_tool.h"
#include "line_render_system.h"
#include "lot_log.h"
#include "lot_mouse_input.h"

#include <cmath>
#include <cstdlib>

namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979f;
const vec3 kTrackColor{1.0f, 1.0f, 0.6f};
const vec3 kBaseColor{1.0f, 0.5f, 0.2f};

float length(const vec3& v) { return std::sqrt(dot(v, v)); }

}  // namespace

const char* TransformTool::modeName() const {
    switch (mode_) {
    case Mode::Move: return "move";
    case Mode::Copy: return "copy";
    case Mode::Rotate: return "rotate";
    case Mode::Scale: return "scale";
    default: return "";
    }
}

bool TransformTool::start(Mode mode, const std::set<LotGameObject::id_t>& selection,
                          const LotCamera& camera, const LotGameObject::Map& objects) {
    saved_.clear();
    created_.clear();
    number_.clear();
    refValid_ = false;
    lastValue_ = 0.0f;

    vec3 sum{0.0f, 0.0f, 0.0f};
    for (LotGameObject::id_t id : selection) {
        if (const auto* obj = LotGameObject::find(objects, id)) {
            saved_.emplace_back(id, obj->transform);
            sum = sum + obj->transform.translation;
        }
    }
    if (saved_.empty()) {
        LOT_LOG("transform: select something first");
        mode_ = Mode::None;
        state_ = State::Idle;
        return false;
    }
    pivot_ = sum * (1.0f / static_cast<float>(saved_.size()));
    mode_ = mode;
    state_ = State::WaitingBase;
    plane_ = SketchPlane::fromCamera(camera);
    // 작업평면은 선택 중심을 지나게 - 바닥 위에 떠 있는 물체도 기준점이 제 높이에 찍힌다
    plane_.origin = pivot_;
    LOT_LOG("transform: " << modeName() << " - click base point (" << saved_.size()
            << " objects, plane " << plane_.name << ")");
    return true;
}

void TransformTool::restore(LotGameObject::Map& objects) {
    for (const auto& entry : saved_) {
        if (auto* obj = LotGameObject::find(objects, entry.first)) obj->transform = entry.second;
    }
}

void TransformTool::cancel(LotGameObject::Map& objects) {
    if (!isActive()) return;
    if (state_ == State::Previewing) restore(objects);
    LOT_LOG("transform: " << modeName() << " cancelled");
    state_ = State::Idle;
    mode_ = Mode::None;
    number_.clear();
}

bool TransformTool::cursorPoint(const Context& ctx, vec3& out) const {
    if (ctx.snap.valid()) {
        out = ctx.snap.point;
        return true;
    }
    const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(),
                                                    ctx.width, ctx.height);
    return plane_.intersect(ray, out);
}

// ---------------------------------------------------------------- preview math

void TransformTool::applyMove(const vec3& delta, LotGameObject::Map& objects) {
    for (const auto& entry : saved_) {
        if (auto* obj = LotGameObject::find(objects, entry.first)) {
            obj->transform = entry.second;
            obj->transform.translation = entry.second.translation + delta;
        }
    }
    trackEnd_ = base_ + delta;
    lastValue_ = length(delta);
}

void TransformTool::applyRotate(float angle, LotGameObject::Map& objects) {
    // 기준점을 피벗으로, 작업평면 법선 둘레. 공전(위치) + 자전(회전) 함께.
    const quat q = quat::angleAxis(angle, plane_.normal);
    for (const auto& entry : saved_) {
        if (auto* obj = LotGameObject::find(objects, entry.first)) {
            obj->transform = entry.second;
            obj->transform.rotation = normalize(q * entry.second.rotation);
            obj->transform.translation = base_ + rotate(q, entry.second.translation - base_);
        }
    }
    lastValue_ = angle * kRadToDeg;
}

void TransformTool::applyScale(float factor, LotGameObject::Map& objects) {
    if (factor < 0.01f) factor = 0.01f;
    if (factor > 100.0f) factor = 100.0f;
    for (const auto& entry : saved_) {
        if (auto* obj = LotGameObject::find(objects, entry.first)) {
            obj->transform = entry.second;
            obj->transform.scale = entry.second.scale * factor;
            // 기준점을 앵커로 - 기준점에 있던 것은 그대로, 나머지는 비례해서 멀어진다
            obj->transform.translation = base_ + (entry.second.translation - base_) * factor;
        }
    }
    lastValue_ = factor;
}

void TransformTool::applyPreview(const vec3& cursor, LotGameObject::Map& objects) {
    trackEnd_ = cursor;
    switch (mode_) {
    case Mode::Move:
    case Mode::Copy:
        applyMove(cursor - base_, objects);
        break;
    case Mode::Rotate: {
        // 각도 0 기준은 기준점 찍은 직후의 첫 커서 방향 - 기준점 자신을 기준으로 하면
        // atan2(0, 0) 이 되어 물체가 튄다 (Vulkan 쪽 rotRefAngle 과 같은 이유).
        const vec3 d = cursor - base_;
        if (length(d) < 1e-5f) return;
        const float a = std::atan2(dot(d, plane_.up), dot(d, plane_.right));
        if (!refValid_) {
            refAngle_ = a;
            refValid_ = true;
        }
        applyRotate(a - refAngle_, objects);
        break;
    }
    case Mode::Scale: {
        // 배율 = (커서 <-> 기준점) / 기준 거리. 기준 거리는 (선택 중심 <-> 기준점) - 기준점을
        // 모서리에 찍었으면 커서가 중심 거리만큼 떨어졌을 때 1 배다. 기준점이 중심 근처라
        // 그 거리가 화면에서 몇 픽셀도 안 되면 배율이 튀므로, 그때는 처음 의미 있게 움직인
        // 커서 거리를 1 배로 삼는다 (기즈모 균등 축척과 같은 느낌).
        const float cur = length(cursor - base_);
        if (baseDist_ < minRef_) {
            if (cur < minRef_) return;
            baseDist_ = cur;
        }
        applyScale(cur / baseDist_, objects);
        break;
    }
    default: break;
    }
}

bool TransformTool::applyNumber(float value, const vec3& cursor, LotGameObject::Map& objects) {
    switch (mode_) {
    case Mode::Move:
    case Mode::Copy: {
        // 거리: 방향은 지금 커서가 가리키는 쪽 (AutoCAD 의 direct distance entry)
        vec3 dir = cursor - base_;
        const float len = length(dir);
        if (len < 1e-5f) {
            LOT_LOG("transform: point the cursor in the move direction first");
            return false;
        }
        applyMove(dir * (value / len), objects);
        return true;
    }
    case Mode::Rotate:
        applyRotate(value * kDegToRad, objects);
        return true;
    case Mode::Scale:
        if (value <= 0.0f) {
            LOT_LOG("transform: scale factor must be positive");
            return false;
        }
        applyScale(value, objects);
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------- flow

void TransformTool::confirm(const Context& ctx, EditHistory& history) {
    if (mode_ == Mode::Copy) {
        // 사본을 미리보기 자리에 만들고 원본은 되돌린다. 도구는 계속 - 다음 사본.
        std::set<LotGameObject::id_t> copies;
        for (const auto& entry : saved_) {
            const auto* src = LotGameObject::find(ctx.objects, entry.first);
            if (!src) continue;
            auto copy = LotGameObject::createGameObject();
            copy.transform = src->transform;  // 미리보기(이동된) 변환
            copy.color = src->color;
            copy.model = src->model;
            copy.material = src->material;
            copy.points = src->points;
            copy.closed = src->closed;
            copy.curve = src->curve;
            const auto id = copy.getId();
            ctx.objects.emplace(id, std::move(copy));
            copies.insert(id);
        }
        restore(ctx.objects);
        history.recordCreated("copy", ctx.objects, copies);
        created_ = copies;
        LOT_LOG("transform: copy placed " << copies.size() << " objects (distance " << lastValue_
                << ") - click for another, Esc to stop");
        number_.clear();
        refValid_ = false;
        return;  // Previewing 유지, base 도 그대로
    }

    EditHistory::Edit edit;
    edit.label = modeName();
    for (const auto& entry : saved_) {
        const auto* obj = LotGameObject::find(ctx.objects, entry.first);
        if (!obj) continue;
        EditHistory::Record before = EditHistory::Record::capture(*obj);
        before.transform = entry.second;
        edit.before.push_back(std::move(before));
        edit.after.push_back(EditHistory::Record::capture(*obj));
    }
    history.record(std::move(edit));
    LOT_LOG("transform: " << modeName() << " done (" << lastValue_
            << (mode_ == Mode::Rotate ? " deg)" : mode_ == Mode::Scale ? " x)" : " units)"));
    state_ = State::Idle;
    mode_ = Mode::None;
    number_.clear();
}

void TransformTool::update(const Context& ctx, EditHistory& history) {
    if (!isActive()) return;

    vec3 cursor;
    const bool have = cursorPoint(ctx, cursor);

    if (state_ == State::Previewing && have && number_.empty()) {
        applyPreview(cursor, ctx.objects);
    }

    if (ctx.mouse.consumeLeftPress()) {
        if (!have) {
            LOT_LOG("transform: cursor misses the work plane - rotate the view");
        } else if (state_ == State::WaitingBase) {
            base_ = cursor;
            trackEnd_ = cursor;
            minRef_ = ctx.camera.worldPerPixel(base_, ctx.height) * 12.0f;
            baseDist_ = length(pivot_ - base_);
            refValid_ = false;
            state_ = State::Previewing;
            LOT_LOG("transform: base point (" << base_.x << ", " << base_.y << ", " << base_.z
                    << ") - click destination or type a value + Enter");
        } else {
            if (number_.empty()) applyPreview(cursor, ctx.objects);
            confirm(ctx, history);
        }
    }
    ctx.mouse.consumeLeftRelease();
}

void TransformTool::finish(const Context& ctx, EditHistory& history) {
    if (!isActive()) return;
    if (state_ == State::WaitingBase) {
        cancel(ctx.objects);  // 기준점도 없이 Enter - 그냥 닫는다
        return;
    }
    if (!number_.empty()) {
        const float value = std::strtof(number_.c_str(), nullptr);
        vec3 cursor;
        if (!cursorPoint(ctx, cursor)) cursor = trackEnd_;
        if (!applyNumber(value, cursor, ctx.objects)) return;  // 방향 없음 등 - 계속 미리보기
    } else if (mode_ == Mode::Copy) {
        // 복사 중 값 없이 Enter = 끝내기 (사본은 클릭으로 놓았을 것이다)
        restore(ctx.objects);
        LOT_LOG("transform: copy finished");
        state_ = State::Idle;
        mode_ = Mode::None;
        return;
    }
    confirm(ctx, history);
}

std::string TransformTool::hint() const {
    if (!isActive()) return "";
    std::string s = modeName();
    if (state_ == State::WaitingBase) {
        s += ": click base point  [Esc cancel]";
        return s;
    }
    switch (mode_) {
    case Mode::Move: s += ": click destination, or type distance + Enter"; break;
    case Mode::Copy: s += ": click where to place the copy (repeat), or distance + Enter; Enter/Esc ends"; break;
    case Mode::Rotate: s += ": click to set angle, or type degrees + Enter"; break;
    case Mode::Scale: s += ": click to set scale, or type factor + Enter"; break;
    default: break;
    }
    if (!number_.empty()) {
        s += "   [" + number_ + "]";
    } else {
        char buf[48];
        if (mode_ == Mode::Rotate) std::snprintf(buf, sizeof(buf), "   %.1f deg", lastValue_);
        else if (mode_ == Mode::Scale) std::snprintf(buf, sizeof(buf), "   x%.3f", lastValue_);
        else std::snprintf(buf, sizeof(buf), "   %.3f", lastValue_);
        s += buf;
    }
    s += "  [Esc cancel]";
    return s;
}

void TransformTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    if (!isActive()) return;
    vec3 cursor;
    const bool have = cursorPoint(ctx, cursor);

    if (state_ == State::WaitingBase) {
        if (have) {
            const float s = ctx.camera.worldPerPixel(cursor, ctx.height) * 6.0f;
            lines.addLine(cursor - plane_.right * s, cursor + plane_.right * s, kBaseColor);
            lines.addLine(cursor - plane_.up * s, cursor + plane_.up * s, kBaseColor);
        }
        return;
    }

    // 기준점 마커 (X) + 고무줄
    const float s = ctx.camera.worldPerPixel(base_, ctx.height) * 6.0f;
    lines.addLine(base_ - plane_.right * s - plane_.up * s, base_ + plane_.right * s + plane_.up * s, kBaseColor);
    lines.addLine(base_ - plane_.right * s + plane_.up * s, base_ + plane_.right * s - plane_.up * s, kBaseColor);
    lines.addLine(base_, trackEnd_, kTrackColor);
    if (mode_ == Mode::Rotate && refValid_) {
        // 0 기준 방향도 보여준다
        const vec3 ref = base_ + (plane_.right * std::cos(refAngle_) + plane_.up * std::sin(refAngle_))
                                 * length(trackEnd_ - base_);
        lines.addLine(base_, ref, vec3{0.6f, 0.6f, 0.6f});
    }
}
