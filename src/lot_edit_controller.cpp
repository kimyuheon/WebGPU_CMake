#include "lot_edit_controller.h"
#include "gizmo_render_system.h"
#include "lot_dimension.h"
#include "line_render_system.h"
#include "lot_model.h"
#include "lot_mouse_input.h"
#include "lot_log.h"

#include <cmath>

namespace {

// 이보다 작게 끌면 박스 선택이 아니라 '빈 곳 클릭'으로 본다
constexpr float kMarqueeMinPx = 3.0f;

// 박스 선택 사각형을 카메라 앞 이 거리에 그린다. 화면 공간 오버레이가 따로
// 없어서 월드 선으로 그리는데, near(0.1) 보다 앞이고 물체보다는 뒤에 있지
// 않을 만큼 가깝게.
constexpr float kMarqueeDepth = 0.5f;

const vec3 kSelectionColor{1.0f, 0.85f, 0.2f};
const vec3 kMarqueeWindowColor{0.4f, 0.7f, 1.0f};    // 왼->오른쪽 (window)
const vec3 kMarqueeCrossingColor{0.4f, 1.0f, 0.5f};  // 오른->왼쪽 (crossing)

}  // namespace

vec3 EditController::pivot(const LotGameObject::Map& objects) const {
    vec3 sum{0.0f, 0.0f, 0.0f};
    int count = 0;
    for (id_t id : selection_) {
        if (const auto* obj = LotGameObject::find(objects, id)) {
            sum = sum + obj->transform.translation;
            ++count;
        }
    }
    return (count > 0) ? sum * (1.0f / static_cast<float>(count)) : sum;
}

void EditController::update(const Context& ctx) {
    auto mouseRay = [&]() {
        return lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(),
                                     ctx.width, ctx.height);
    };

    // 지워진 오브젝트가 선택에 남아 있으면 뺀다
    for (auto it = selection_.begin(); it != selection_.end();) {
        if (LotGameObject::find(ctx.objects, *it) == nullptr) it = selection_.erase(it);
        else ++it;
    }

    // 1. 커서 아래 스냅 후보.
    //
    // 점을 묻고 있을 때만 찾는다 - 열린 도구가 있거나 기즈모를 끌고 있을 때. CAD 에서
    // osnap 은 명령이 점을 기다릴 때만 뜬다. 그냥 고르려고 커서를 옮기는데 마커가
    // 깜빡이면 눈이 아프고, 쓰지도 않을 정밀 피킹을 프레임마다 돌리는 셈이기도 하다.
    // 드래그 중이면 끌고 있는 것들은 후보에서 뺀다 (제 정점에 붙지 않도록).
    if (!ctx.toolActive && !drag_.active) {
        snap_ = lot_osnap::Snap{};
    } else {
        lot_osnap::Query query;
        query.camera = &ctx.camera;
        query.mouseX = ctx.mouse.x();
        query.mouseY = ctx.mouse.y();
        query.width = ctx.width;
        query.height = ctx.height;
        query.radiusPx = snapRadiusPx;
        query.exclude = (drag_.active || ctx.excludeSelectionFromSnap) ? &selection_ : nullptr;
        query.fromPoint = ctx.snapFromPoint;
        const lot_osnap::Kind before = snap_.kind;
        snap_ = lot_osnap::find(query, mouseRay(), ctx.objects);
        // 종류가 바뀔 때만 한 줄 - 어떤 스냅이 잡혔는지 상태바에서 따라갈 수 있게
        if (snap_.kind != before && snap_.valid()) {
            LOT_LOG("snap: " << lot_osnap::kindName(snap_.kind) << " of object " << snap_.id);
        }
    }

    // 2. 누름 (도구가 열려 있으면 클릭은 그 도구가 가져간다)
    const bool doubleClick = ctx.mouse.consumeLeftDoubleClick();
    if (!ctx.toolActive && ctx.mouse.consumeLeftPress()) {
        const lot_pick::Ray ray = mouseRay();
        const bool shift = ctx.mouse.shiftAtPress();

        // 2-1. 기즈모 핸들? 오브젝트보다 먼저 - 기즈모가 위에 겹쳐 있다.
        //      단 더블 클릭은 '이걸 열어라' 지 '끌어라' 가 아니다. 첫 클릭으로 선택되면
        //      기즈모가 커서 밑에 생기므로, 그대로 두면 두 번째 누름이 늘 기즈모를 잡는다.
        const int handle = (hasSelection() && !doubleClick)
            ? ctx.gizmo.hitTest(ray, ctx.camera, pivot(ctx.objects)) : -1;
        if (handle >= 0) {
            beginGizmoDrag(ctx, ray, handle);
        } else {
            // 2-2. 오브젝트? 메시가 먼저, 없으면 커서 근처의 스케치 선.
            //      (메시 위에 선이 겹쳐 있어도 메시가 이긴다 - 선은 스냅으로 집기 쉽다)
            const lot_pick::Hit hit = lot_pick::pickObjectPrecise(ray, ctx.objects);
            id_t picked = hit.id;
            float sketchDist = 0.0f;
            if (!hit.valid()) {
                picked = lot_pick::pickSketch(ctx.camera, ctx.mouse.x(), ctx.mouse.y(),
                                              ctx.width, ctx.height, sketchPickPx,
                                              ctx.objects, sketchDist);
            }
            if (picked != LotGameObject::kInvalidId) {
                if (shift) {
                    // 토글
                    if (isSelected(picked)) selection_.erase(picked);
                    else selection_.insert(picked);
                } else {
                    selection_ = {picked};
                }
                if (doubleClick) doubleClicked_ = picked;
                if (hit.valid()) {
                    LOT_LOG("pick: object " << picked << " tri " << hit.triangle
                            << " -> " << selection_.size() << " selected");
                } else {
                    LOT_LOG("pick: sketch " << picked << " (" << sketchDist << "px) -> "
                            << selection_.size() << " selected");
                }
            } else {
                // 2-3. 빈 곳: 박스 선택 시작. 뗄 때 크기를 보고 클릭인지 판단한다.
                marquee_.active = true;
                marquee_.x0 = ctx.mouse.x();
                marquee_.y0 = ctx.mouse.y();
            }
        }
    }

    // 3. 드래그 진행 / 끝
    if (drag_.active) {
        if (!ctx.mouse.isLeftDown()) {
            endGizmoDrag(ctx);
        } else {
            updateGizmoDrag(ctx, mouseRay());
        }
    }

    // 4. 박스 선택 끝
    if (marquee_.active && !ctx.mouse.isLeftDown()) {
        marquee_.active = false;
        finishMarquee(ctx, ctx.mouse.x(), ctx.mouse.y(), ctx.mouse.shiftAtPress());
    }

    ctx.mouse.consumeLeftRelease();  // 위에서 isLeftDown 으로 봤으므로 플래그만 비운다
}

bool EditController::screenAngleAroundPivot(const Context& ctx, float& angleOut,
                                            float& distOut) const {
    float px, py;
    if (!ctx.camera.projectToScreen(drag_.pivotStart, ctx.width, ctx.height, px, py)) {
        return false;
    }
    const float dx = ctx.mouse.x() - px;
    const float dy = ctx.mouse.y() - py;
    angleOut = std::atan2(dy, dx);
    distOut = std::sqrt(dx * dx + dy * dy);
    return true;
}

void EditController::beginGizmoDrag(const Context& ctx, const lot_pick::Ray& ray, int handle) {
    const vec3 origin = pivot(ctx.objects);
    const auto mode = ctx.gizmo.mode;
    drag_.pivotStart = origin;  // 아래 screenAngleAroundPivot 이 쓴다

    bool ok = false;
    if (mode == GizmoRenderSystem::Mode::Rotate) {
        // 회전: 화면에서 기준점 둘레 각. 링을 옆에서 볼 때도 안정적이다.
        float dist = 0.0f;
        ok = screenAngleAroundPivot(ctx, drag_.startAngle, dist);
    } else if (mode == GizmoRenderSystem::Mode::Scale) {
        if (handle == GizmoRenderSystem::kHandleUniform) {
            // 균등: 화면에서 기준점까지의 거리 비율
            float angle = 0.0f;
            ok = screenAngleAroundPivot(ctx, angle, drag_.startScreenDist);
            ok = ok && drag_.startScreenDist > 2.0f;
        } else {
            ok = lot_pick::closestPointOnLine(
                ray, origin, GizmoRenderSystem::axisDirection(handle), drag_.startS);
            ok = ok && std::fabs(drag_.startS) > 1e-4f;
        }
    } else if (GizmoRenderSystem::isPlaneHandle(handle)) {
        ok = GizmoRenderSystem::intersectPlane(
            ray, origin, GizmoRenderSystem::planeNormalAxis(handle), drag_.startHit);
    } else {
        ok = lot_pick::closestPointOnLine(
            ray, origin, GizmoRenderSystem::axisDirection(handle), drag_.startS);
    }
    if (!ok) return;

    drag_.active = true;
    drag_.mode = static_cast<int>(mode);
    drag_.handle = handle;
    drag_.startTransforms.clear();
    for (id_t id : selection_) {
        if (const auto* obj = LotGameObject::find(ctx.objects, id)) {
            drag_.startTransforms.emplace_back(id, obj->transform);
        }
    }
    static const char* kModeNames[] = {"move", "rotate", "scale"};
    LOT_LOG("drag: start " << kModeNames[drag_.mode] << " on handle " << handle
            << ", " << drag_.startTransforms.size() << " objects");
}

void EditController::applyRotation(const Context& ctx, int axis, float angle) {
    // 월드 축 둘레로 기준점을 중심 삼아 돌린다.
    // 회전: q_new = q_axis * q_start (오브젝트 자기 회전 뒤에 월드 회전).
    // 위치: 기준점에서의 오프셋을 같은 회전으로 돌린다 - 여러 개면 서로의 배치가 유지된다.
    // 매 프레임 '시작값 + 이번 각도'로 다시 계산하므로 오차가 쌓이지 않는다.
    const quat q = quat::angleAxis(angle, GizmoRenderSystem::axisDirection(axis));
    for (const auto& entry : drag_.startTransforms) {
        auto* obj = LotGameObject::find(ctx.objects, entry.first);
        if (!obj) continue;
        const TransformComponent& start = entry.second;
        obj->transform.rotation = normalize(q * start.rotation);
        const vec3 offset = start.translation - drag_.pivotStart;
        obj->transform.translation = drag_.pivotStart + rotate(q, offset);
    }
}

void EditController::applyScale(const Context& ctx, const vec3& factor) {
    // 기준점을 중심으로 축마다 factor 배. 위치도 같은 비율로 기준점에서 멀어진다.
    for (const auto& entry : drag_.startTransforms) {
        auto* obj = LotGameObject::find(ctx.objects, entry.first);
        if (!obj) continue;
        const TransformComponent& start = entry.second;
        obj->transform.scale = vec3{start.scale.x * factor.x, start.scale.y * factor.y,
                                    start.scale.z * factor.z};
        const vec3 offset = start.translation - drag_.pivotStart;
        obj->transform.translation = drag_.pivotStart
            + vec3{offset.x * factor.x, offset.y * factor.y, offset.z * factor.z};
    }
}

void EditController::applyTranslation(const Context& ctx, const vec3& delta) {
    for (const auto& entry : drag_.startTransforms) {
        if (auto* obj = LotGameObject::find(ctx.objects, entry.first)) {
            obj->transform.translation = entry.second.translation + delta;
        }
    }
}

void EditController::updateGizmoDrag(const Context& ctx, const lot_pick::Ray& ray) {
    if (drag_.mode == static_cast<int>(GizmoRenderSystem::Mode::Rotate)) {
        float angle = 0.0f, dist = 0.0f;
        if (!screenAngleAroundPivot(ctx, angle, dist)) return;
        // 화면에서 잰 각의 방향은 축이 카메라를 향하는지 등지는지에 따라 뒤집힌다.
        // 축이 카메라 쪽(-forward)이면 화면의 시계 방향이 축 둘레의 양의 회전이다.
        const vec3 axisDir = GizmoRenderSystem::axisDirection(drag_.handle);
        const float facing = dot(axisDir, ctx.camera.getForward());
        const float sign = (facing < 0.0f) ? 1.0f : -1.0f;
        applyRotation(ctx, drag_.handle, sign * (angle - drag_.startAngle));
        return;
    }

    if (drag_.mode == static_cast<int>(GizmoRenderSystem::Mode::Scale)) {
        auto clampFactor = [](float f) { return std::fmax(0.01f, std::fmin(f, 100.0f)); };
        if (drag_.handle == GizmoRenderSystem::kHandleUniform) {
            float angle = 0.0f, dist = 0.0f;
            if (!screenAngleAroundPivot(ctx, angle, dist)) return;
            const float f = clampFactor(dist / drag_.startScreenDist);
            applyScale(ctx, vec3{f, f, f});
        } else {
            const vec3 axisDir = GizmoRenderSystem::axisDirection(drag_.handle);
            float sNow = 0.0f;
            if (!lot_pick::closestPointOnLine(ray, drag_.pivotStart, axisDir, sNow)) return;
            const float f = clampFactor(sNow / drag_.startS);
            vec3 factor{1.0f, 1.0f, 1.0f};
            if (drag_.handle == 0) factor.x = f;
            else if (drag_.handle == 1) factor.y = f;
            else factor.z = f;
            applyScale(ctx, factor);
        }
        return;
    }

    const bool plane = GizmoRenderSystem::isPlaneHandle(drag_.handle);
    vec3 delta{0.0f, 0.0f, 0.0f};

    if (snap_.valid()) {
        // 스냅: 기준점(선택 중심)을 스냅 점에 맞춘다. 축/평면 구속은 지킨다 -
        // 축 드래그면 스냅 점을 축에 투영하고, 평면이면 평면에 투영한다.
        const vec3 target = snap_.point - drag_.pivotStart;
        if (plane) {
            const vec3 n = GizmoRenderSystem::axisDirection(
                GizmoRenderSystem::planeNormalAxis(drag_.handle));
            delta = target - n * dot(target, n);
        } else {
            const vec3 axisDir = GizmoRenderSystem::axisDirection(drag_.handle);
            delta = axisDir * dot(target, axisDir);
        }
    } else if (plane) {
        // 평면: 지금 교점과 시작 교점의 차이만큼. 둘 다 평면 위라 차이도 평면 위다.
        vec3 hit;
        if (!GizmoRenderSystem::intersectPlane(
                ray, drag_.pivotStart, GizmoRenderSystem::planeNormalAxis(drag_.handle), hit)) {
            return;
        }
        delta = hit - drag_.startHit;
    } else {
        // 축: 마우스 레이가 축 위의 어디를 가리키는지로
        const vec3 axisDir = GizmoRenderSystem::axisDirection(drag_.handle);
        float s = 0.0f;
        if (!lot_pick::closestPointOnLine(ray, drag_.pivotStart, axisDir, s)) return;
        delta = axisDir * (s - drag_.startS);
    }

    applyTranslation(ctx, delta);
}

void EditController::endGizmoDrag(const Context& ctx) {
    drag_.active = false;

    // 히스토리: 누른 순간의 변환 -> 지금 변환. 안 움직였으면 record 가 걸러낸다.
    {
        static const char* kLabels[] = {"move", "rotate", "scale"};
        EditHistory::Edit edit;
        edit.label = kLabels[drag_.mode];
        for (const auto& entry : drag_.startTransforms) {
            const auto* obj = LotGameObject::find(ctx.objects, entry.first);
            if (!obj) continue;
            EditHistory::Record before = EditHistory::Record::capture(*obj);
            before.transform = entry.second;
            edit.before.push_back(std::move(before));
            edit.after.push_back(EditHistory::Record::capture(*obj));
        }
        history_.record(std::move(edit));
    }

    if (snap_.valid()) {
        LOT_LOG("snap: " << lot_osnap::kindName(snap_.kind)
                << " of object " << snap_.id << " (" << snap_.screenDistance << "px)");
    }
    const vec3 p = pivot(ctx.objects);
    LOT_LOG("drag: end, pivot at (" << p.x << ", " << p.y << ", " << p.z << ")");
    if (!drag_.startTransforms.empty()) {
        if (const auto* obj = LotGameObject::find(ctx.objects, drag_.startTransforms[0].first)) {
            const auto& t = obj->transform;
            const vec3 e = t.eulerAngles() * (180.0f / 3.14159265f);
            LOT_LOG("drag: first object rot deg (" << e.x << ", " << e.y << ", " << e.z
                    << ") quat (" << t.rotation.w << ", " << t.rotation.x << ", "
                    << t.rotation.y << ", " << t.rotation.z << ") scale ("
                    << t.scale.x << ", " << t.scale.y << ", " << t.scale.z << ")");
        }
    }
}

void EditController::finishMarquee(const Context& ctx, float x1, float y1, bool additive) {
    const float x0 = marquee_.x0, y0 = marquee_.y0;
    const float w = std::fabs(x1 - x0), h = std::fabs(y1 - y0);

    // 거의 안 움직였으면 빈 곳 클릭 = 전체 해제 (Shift 면 그대로)
    if (w < kMarqueeMinPx && h < kMarqueeMinPx) {
        if (!additive && hasSelection()) {
            selection_.clear();
            LOT_LOG("pick: nothing (deselected)");
        }
        return;
    }

    // 왼->오른쪽 = window (완전히 들어온 것만), 오른->왼쪽 = crossing (걸치면).
    const bool crossing = x1 < x0;
    const float minX = std::fmin(x0, x1), maxX = std::fmax(x0, x1);
    const float minY = std::fmin(y0, y1), maxY = std::fmax(y0, y1);

    if (!additive) selection_.clear();

    // 점 집합을 화면에 투영해 상자와 비교. window 는 전부, crossing 은 하나라도.
    auto testPoints = [&](const mat4& m, const std::vector<vec3>& points) {
        bool any = false, all = true;
        for (const vec3& p : points) {
            float px, py;
            const bool inside = ctx.camera.projectToScreen(transformPoint(m, p), ctx.width,
                                                           ctx.height, px, py)
                && px >= minX && px <= maxX && py >= minY && py <= maxY;
            if (inside) any = true;
            else all = false;
            if (crossing ? any : !all) break;  // 결론이 났으면 그만
        }
        return crossing ? any : all;
    };

    for (const auto& entry : ctx.objects) {
        const LotGameObject& obj = entry.second;
        if (!lot_pick::isSelectable(obj)) continue;  // 꺼지거나 잠긴 층
        const mat4 m = obj.transform.mat4Transform();
        bool selected = false;
        if (obj.model) {
            // 모델의 모든 정점을 화면에 투영해서 본다. 경계 상자 꼭짓점보다 정확하고
            // (회전한 상자의 꼭짓점은 실제 실루엣보다 크다) 정점 수천 개는 값싸다.
            selected = testPoints(m, obj.model->getPositions());
        } else if (obj.isSketch()) {
            selected = testPoints(m, obj.points);
        } else if (obj.isDimension()) {
            selected = testPoints(mat4::identity(), lot_dim::outlinePoints(obj));  // 이미 월드
        } else if (obj.isText()) {
            vec3 c[4];
            if (lot_text::quadCorners(obj, c)) selected = testPoints(mat4::identity(), {c[0], c[1], c[2], c[3]});
        }
        if (selected) selection_.insert(entry.first);
    }
    LOT_LOG("marquee: " << (crossing ? "crossing" : "window") << " -> "
            << selection_.size() << " selected");
}

void EditController::duplicateSelection(LotGameObject::Map& objects) {
    if (selection_.empty()) return;

    std::set<id_t> copies;
    for (id_t id : selection_) {
        const auto* src = LotGameObject::find(objects, id);
        if (!src) continue;

        auto copy = LotGameObject::createGameObject();
        copy.transform = src->transform;
        copy.color = src->color;
        copy.layer = src->layer;
        copy.linetype = src->linetype;
        copy.colorByLayer = src->colorByLayer;
        copy.model = src->model;        // 공유 - GPU 버퍼 복사 없음
        copy.material = src->material;  // 공유
        copy.points = src->points;      // 스케치는 점을 복사 (GPU 자원이 아니다)
        copy.closed = src->closed;
        copy.curve = src->curve;
        copy.dim = src->dim;
        copy.text = src->text;
        const id_t newId = copy.getId();
        objects.emplace(newId, std::move(copy));
        copies.insert(newId);
    }
    LOT_LOG("copy: " << copies.size() << " objects duplicated");
    history_.recordCreated("copy", objects, copies);
    selection_ = std::move(copies);
}

void EditController::deleteSelection(LotGameObject::Map& objects) {
    if (selection_.empty()) return;
    drag_.active = false;

    EditHistory::Edit edit;
    edit.label = "delete";
    edit.before = EditHistory::snapshot(objects, selection_);

    size_t removed = 0;
    for (id_t id : selection_) {
        removed += objects.erase(id);
    }
    LOT_LOG("delete: " << removed << " objects removed");
    history_.record(std::move(edit));
    selection_.clear();
}

void EditController::undo(LotGameObject::Map& objects) {
    drag_.active = false;
    marquee_.active = false;
    std::set<id_t> touched = history_.undo(objects);
    if (touched.empty()) return;
    // 되살아난/되돌아간 것들을 선택해 무엇이 바뀌었는지 보여준다. 지워진 것은 뺀다.
    selection_.clear();
    for (id_t id : touched) {
        if (LotGameObject::find(objects, id)) selection_.insert(id);
    }
}

void EditController::redo(LotGameObject::Map& objects) {
    drag_.active = false;
    marquee_.active = false;
    std::set<id_t> touched = history_.redo(objects);
    if (touched.empty()) return;
    selection_.clear();
    for (id_t id : touched) {
        if (LotGameObject::find(objects, id)) selection_.insert(id);
    }
}

void EditController::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    // 선택 상자 (OBB). 오브젝트 변환을 그대로 타서 회전하면 같이 돈다.
    for (id_t id : selection_) {
        const auto* obj = LotGameObject::find(ctx.objects, id);
        if (!obj) continue;
        if (obj->model) {
            lines.addTransformedBox(obj->model->boundsMin(), obj->model->boundsMax(),
                                    obj->transform.mat4Transform(), kSelectionColor);
        } else if (obj->isSketch()) {
            // 선은 상자 대신 자기 자신을 선택 색으로 한 번 더 그린다
            const std::vector<vec3> pts = obj->worldPoints();
            const size_t n = pts.size();
            const size_t segments = obj->closed ? n : n - 1;
            for (size_t i = 0; i < segments; ++i) {
                lines.addLine(pts[i], pts[(i + 1) % n], kSelectionColor);
            }
        } else if (obj->isDimension()) {
            const lot_dim::Geometry g = lot_dim::build(obj->dim, obj->transform.mat4Transform(), nullptr);
            for (const auto& s : g.segments) lines.addLine(s.first, s.second, kSelectionColor);
        } else if (obj->isText()) {
            vec3 c[4];
            if (lot_text::quadCorners(*obj, c)) {
                for (int i = 0; i < 4; ++i) lines.addLine(c[i], c[(i + 1) % 4], kSelectionColor);
            }
        }
    }

    // 박스 선택 사각형. 화면 좌표 네 귀퉁이를 카메라 앞 고정 거리의 월드 점으로.
    if (marquee_.active) {
        const float x1 = ctx.mouse.x(), y1 = ctx.mouse.y();
        const bool crossing = x1 < marquee_.x0;
        const vec3 color = crossing ? kMarqueeCrossingColor : kMarqueeWindowColor;
        auto corner = [&](float sx, float sy) {
            const lot_pick::Ray r = lot_pick::screenToRay(ctx.camera, sx, sy, ctx.width, ctx.height);
            return r.origin + r.direction * kMarqueeDepth;
        };
        const vec3 c0 = corner(marquee_.x0, marquee_.y0);
        const vec3 c1 = corner(x1, marquee_.y0);
        const vec3 c2 = corner(x1, y1);
        const vec3 c3 = corner(marquee_.x0, y1);
        lines.addLine(c0, c1, color);
        lines.addLine(c1, c2, color);
        lines.addLine(c2, c3, color);
        lines.addLine(c3, c0, color);
    }

    // 스냅 마커
    lot_osnap::addMarker(lines, snap_, ctx.camera, ctx.height, snapMarkerPx);
}

void EditController::drawGizmo(FrameInfo& frame, GizmoRenderSystem& gizmo) const {
    if (!hasSelection()) return;
    gizmo.render(frame, pivot(frame.gameObjects), drag_.active ? drag_.handle : -1);
}
