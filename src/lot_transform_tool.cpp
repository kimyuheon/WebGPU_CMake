#include "lot_transform_tool.h"
#include "lot_cursor_snap.h"
#include "line_render_system.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "lot_model.h"

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
    case Mode::Mirror: return "mirror";
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
    mirrorValid_ = false;
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
    if (!plane_.intersect(ray, out)) return false;
    // 보정은 이동/복사에만 - 회전 각과 축척 배율은 방향이 아니라 값이라 눈금이 의미 없다.
    if (isPreviewing() && (mode_ == Mode::Move || mode_ == Mode::Copy || mode_ == Mode::Mirror)) {
        out = lot_cursor::apply(out, plane_, &base_);
    } else if (state_ == State::WaitingBase) {
        out = lot_cursor::apply(out, plane_, nullptr);  // 기준점은 눈금에 맞출 수 있다
    }
    return true;
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
    case Mode::Mirror: {
        // 객체는 건드리지 않는다 - 대칭 사본은 확정 때 만들고, 미리보기는 drawOverlay 가 선으로
        vec3 n;
        mirrorEnd_ = cursor;
        mirrorValid_ = mirrorAxis(cursor, n);
        const vec3 d = cursor - base_;
        lastValue_ = std::atan2(dot(d, plane_.up), dot(d, plane_.right)) * kRadToDeg;
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
    case Mode::Mirror: {
        // 대칭축 각도 (도, 작업평면 오른쪽 기준 반시계)
        const float a = value * kDegToRad;
        vec3 n;
        mirrorEnd_ = base_ + (plane_.right * std::cos(a) + plane_.up * std::sin(a)) * std::fmax(minRef_, 1e-3f) * 4.0f;
        mirrorValid_ = mirrorAxis(mirrorEnd_, n);
        lastValue_ = value;
        return mirrorValid_;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------- mirror

bool TransformTool::mirrorAxis(const vec3& p, vec3& normalOut) const {
    vec3 a = p - base_;
    a = a - plane_.normal * dot(a, plane_.normal);   // 작업평면 위로
    const float len = length(a);
    if (len < std::fmax(minRef_ * 0.25f, 1e-6f)) return false;
    // 반사 평면 = 축 방향과 작업평면 법선을 품는 평면. 그 법선은 둘에 수직.
    normalOut = normalize(cross(a * (1.0f / len), plane_.normal));
    return true;
}

LotGameObject TransformTool::mirrored(const LotGameObject& src, const vec3& origin, const vec3& n) {
    auto refl = [&](const vec3& p) { return p - n * (2.0f * dot(p - origin, n)); };
    auto reflDir = [&](const vec3& v) { return v - n * (2.0f * dot(v, n)); };

    LotGameObject out = LotGameObject::createGameObject();
    out.color = src.color;
    out.layer = src.layer;
    out.linetype = src.linetype;
    out.colorByLayer = src.colorByLayer;
    out.model = src.model;
    out.material = src.material;
    out.points = src.points;
    out.closed = src.closed;
    out.curve = src.curve;
    out.dim = src.dim;
    out.text = src.text;
    out.light = src.light;
    out.transform = src.transform;

    const TransformComponent& T = src.transform;
    out.transform.translation = refl(T.translation);

    if (src.model) {
        // 메시: 반사 M 은 회전으로만 못 쓴다. M R = (M R D) D 로 나눠 회전 M R D 와 로컬 x 축
        // 뒤집기 D 를 쓴다. M R D = R (Rᵀ M R) D 이고, 두 반사(D 다음 로컬 반사)의 곱은
        // 쿼터니언 nl * ex 회전이다 (nl = 로컬로 옮긴 반사 법선).
        const vec3 nl = rotate(T.rotation.conjugate(), n);
        const quat q = normalize(quat{-nl.x, 0.0f, nl.z, -nl.y});   // (0,nl)(0,ex) = (-nl·ex, nl×ex)
        out.transform.rotation = normalize(T.rotation * q);
        out.transform.scale = vec3{-T.scale.x, T.scale.y, T.scale.z};
        return out;
    }

    // 점으로 된 것: 월드로 꺼내 반사한 뒤, 회전/축척은 그대로 두고 반사된 위치에서 다시 로컬로
    const mat4 M = T.mat4Transform();
    const TransformComponent& T2 = out.transform;
    auto mirrorPoint = [&](const vec3& local) { return T2.worldToLocalPoint(refl(transformPoint(M, local))); };
    auto mirrorDir = [&](const vec3& local) {
        return T2.worldToLocalDirection(reflDir(transformPoint(M, local) - T.translation));
    };
    for (vec3& p : out.points) p = mirrorPoint(p);
    if (src.hasCurve()) {
        // 축 둘 다 반사하면 왼손 좌표가 된다 - 위 축을 뒤집고 각도 범위를 (-끝, -시작) 으로
        // 바꾸면 같은 곡선을 오른손 좌표로 다시 쓴 것이 된다 (내보내기 · 스냅이 그대로 쓴다).
        out.curve.center = mirrorPoint(src.curve.center);
        out.curve.right = mirrorDir(src.curve.right);
        out.curve.up = mirrorDir(src.curve.up) * -1.0f;
        out.curve.start = -src.curve.end;
        out.curve.end = -src.curve.start;
    }
    if (src.isDimension()) {
        out.dim.p1 = mirrorPoint(src.dim.p1);
        out.dim.p2 = mirrorPoint(src.dim.p2);
        out.dim.dimLine = mirrorPoint(src.dim.dimLine);
    }
    if (src.isText()) {
        // 글자는 뒤집지 않는다 (AutoCAD MIRRTEXT 0). 대신 맞춤을 뒤집어 글자 상자가 대칭 자리에
        // 오게 한다: 축이 글자 진행 방향을 가로지르면 좌우, 나란하면 위아래.
        const vec3 rw = normalize(transformPoint(M, src.text.right) - T.translation);
        const vec3 uw = normalize(transformPoint(M, src.text.up) - T.translation);
        if (std::fabs(dot(n, rw)) >= std::fabs(dot(n, uw))) out.text.hAlign = 2 - src.text.hAlign;
        else out.text.vAlign = 2 - src.text.vAlign;
    }
    return out;
}

void TransformTool::confirmMirror(const Context& ctx, EditHistory& history, bool eraseSource) {
    vec3 n;
    if (!mirrorAxis(mirrorEnd_, n)) {
        LOT_LOG("transform: mirror line is too short - pick a second point away from the first");
        return;
    }
    EditHistory::Edit edit;
    edit.label = "mirror";
    std::set<LotGameObject::id_t> sources, copies;
    for (const auto& entry : saved_) sources.insert(entry.first);
    if (eraseSource) edit.before = EditHistory::snapshot(ctx.objects, sources);
    for (const auto& entry : saved_) {
        const LotGameObject* src = LotGameObject::find(ctx.objects, entry.first);
        if (!src) continue;
        LotGameObject copy = mirrored(*src, base_, n);
        const auto id = copy.getId();
        ctx.objects.emplace(id, std::move(copy));
        copies.insert(id);
    }
    edit.after = EditHistory::snapshot(ctx.objects, copies);
    if (eraseSource) {
        for (LotGameObject::id_t id : sources) ctx.objects.erase(id);
    }
    history.record(std::move(edit));
    created_ = copies;
    LOT_LOG("transform: mirror done - " << copies.size() << " objects"
            << (eraseSource ? ", source erased" : ", source kept"));
    state_ = State::Idle;
    mode_ = Mode::None;
    number_.clear();
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
        } else if (mode_ == Mode::Mirror) {
            if (number_.empty()) applyPreview(cursor, ctx.objects);
            confirmMirror(ctx, history, ctx.mouse.shiftAtPress());
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
        if (mode_ == Mode::Mirror) { confirmMirror(ctx, history, false); return; }
    } else if (mode_ == Mode::Mirror) {
        confirmMirror(ctx, history, false);   // 지금 커서 축으로, 원본은 둔다
        return;
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
        s += mode_ == Mode::Mirror ? ": click the first point of the mirror line  [Esc cancel]"
                                   : ": click base point  [Esc cancel]";
        return s;
    }
    switch (mode_) {
    case Mode::Move: s += ": click destination, or type distance + Enter"; break;
    case Mode::Copy: s += ": click where to place the copy (repeat), or distance + Enter; Enter/Esc ends"; break;
    case Mode::Rotate: s += ": click to set angle, or type degrees + Enter"; break;
    case Mode::Scale: s += ": click to set scale, or type factor + Enter"; break;
    case Mode::Mirror: s += ": click the second point of the mirror line (Shift+click erases the source), or type angle + Enter"; break;
    default: break;
    }
    if (!number_.empty()) {
        s += "   [" + number_ + "]";
    } else {
        char buf[48];
        if (mode_ == Mode::Rotate || mode_ == Mode::Mirror) std::snprintf(buf, sizeof(buf), "   %.1f deg", lastValue_);
        else if (mode_ == Mode::Scale) std::snprintf(buf, sizeof(buf), "   x%.3f", lastValue_);
        else std::snprintf(buf, sizeof(buf), "   %.3f", lastValue_);
        s += buf;
    }
    s += "  [Esc cancel]";
    s += lot_cursor::statusSuffix();
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
    if (mode_ == Mode::Mirror) {
        // 대칭축 (길게) + 대칭된 모습 (선으로)
        vec3 n;
        if (!mirrorAxis(mirrorEnd_, n)) return;
        const vec3 dir = normalize(cross(plane_.normal, n));
        const float reach = ctx.camera.worldPerPixel(base_, ctx.height) * 4000.0f;
        lines.addLine(base_ - dir * reach, base_ + dir * reach, vec3{0.5f, 0.5f, 0.5f});
        lines.addLine(base_, mirrorEnd_, kTrackColor);
        // 미리보기는 월드 점을 바로 반사해서 그린다 (사본은 확정 때만 만든다 - id 를 쓰지 않게).
        // 선택이 아주 많으면 앞의 것만 (도면 전체를 고르고 대칭해도 프레임이 버티게).
        const vec3 ghost{0.4f, 0.8f, 1.0f};
        auto refl = [&](const vec3& p) { return p - n * (2.0f * dot(p - base_, n)); };
        size_t drawn = 0;
        for (const auto& entry : saved_) {
            if (++drawn > 2000) break;
            const LotGameObject* src = LotGameObject::find(ctx.objects, entry.first);
            if (!src) continue;
            if (src->isSketch()) {
                const std::vector<vec3> pts = src->worldPoints();
                const size_t cnt = pts.size();
                const size_t segs = src->closed ? cnt : (cnt ? cnt - 1 : 0);
                for (size_t i = 0; i < segs; ++i) lines.addLine(refl(pts[i]), refl(pts[(i + 1) % cnt]), ghost);
            } else if (src->model) {
                const mat4 m = src->transform.mat4Transform();
                const vec3& lo = src->model->boundsMin();
                const vec3& hi = src->model->boundsMax();
                vec3 c[8];
                for (int i = 0; i < 8; ++i) {
                    c[i] = refl(transformPoint(m, vec3{(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z}));
                }
                const int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
                for (const auto& k : e) lines.addLine(c[k[0]], c[k[1]], ghost);
            } else {
                lines.addCross(refl(src->transform.translation), ctx.camera.worldPerPixel(base_, ctx.height) * 8.0f, ghost);
            }
        }
        return;
    }
    lines.addLine(base_, trackEnd_, kTrackColor);
    if (mode_ == Mode::Rotate && refValid_) {
        // 0 기준 방향도 보여준다
        const vec3 ref = base_ + (plane_.right * std::cos(refAngle_) + plane_.up * std::sin(refAngle_))
                                 * length(trackEnd_ - base_);
        lines.addLine(base_, ref, vec3{0.6f, 0.6f, 0.6f});
    }
}
