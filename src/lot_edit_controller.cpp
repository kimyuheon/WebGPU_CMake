#include "lot_edit_controller.h"
#include "gizmo_render_system.h"
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

    // 1. 커서 아래 스냅 후보. 드래그 중이면 끌고 있는 것들은 뺀다.
    {
        lot_osnap::Query query;
        query.camera = &ctx.camera;
        query.mouseX = ctx.mouse.x();
        query.mouseY = ctx.mouse.y();
        query.width = ctx.width;
        query.height = ctx.height;
        query.radiusPx = snapRadiusPx;
        query.exclude = drag_.active ? &selection_ : nullptr;
        snap_ = lot_osnap::find(query, mouseRay(), ctx.objects);
    }

    // 2. 누름
    if (ctx.mouse.consumeLeftPress()) {
        const lot_pick::Ray ray = mouseRay();
        const bool shift = ctx.mouse.shiftAtPress();

        // 2-1. 기즈모 핸들? 오브젝트보다 먼저 - 기즈모가 위에 겹쳐 있다.
        const int handle = hasSelection()
            ? ctx.gizmo.hitTest(ray, ctx.camera, pivot(ctx.objects)) : -1;
        if (handle >= 0) {
            beginGizmoDrag(ctx, ray, handle);
        } else {
            // 2-2. 오브젝트?
            const lot_pick::Hit hit = lot_pick::pickObjectPrecise(ray, ctx.objects);
            if (hit.valid()) {
                if (shift) {
                    // 토글
                    if (isSelected(hit.id)) selection_.erase(hit.id);
                    else selection_.insert(hit.id);
                } else {
                    selection_ = {hit.id};
                }
                LOT_LOG("pick: object " << hit.id << " tri " << hit.triangle
                        << " -> " << selection_.size() << " selected");
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

void EditController::beginGizmoDrag(const Context& ctx, const lot_pick::Ray& ray, int handle) {
    const vec3 origin = pivot(ctx.objects);
    bool ok = false;
    if (GizmoRenderSystem::isPlaneHandle(handle)) {
        ok = GizmoRenderSystem::intersectPlane(
            ray, origin, GizmoRenderSystem::planeNormalAxis(handle), drag_.startHit);
    } else {
        ok = lot_pick::closestPointOnLine(
            ray, origin, GizmoRenderSystem::axisDirection(handle), drag_.startS);
    }
    if (!ok) return;

    drag_.active = true;
    drag_.handle = handle;
    drag_.pivotStart = origin;
    drag_.startTransforms.clear();
    for (id_t id : selection_) {
        if (const auto* obj = LotGameObject::find(ctx.objects, id)) {
            drag_.startTransforms.emplace_back(id, obj->transform);
        }
    }
    LOT_LOG("drag: start on " << (GizmoRenderSystem::isPlaneHandle(handle) ? "plane " : "axis ")
            << handle << ", " << drag_.startTransforms.size() << " objects");
}

void EditController::applyTranslation(const Context& ctx, const vec3& delta) {
    for (const auto& entry : drag_.startTransforms) {
        if (auto* obj = LotGameObject::find(ctx.objects, entry.first)) {
            obj->transform.translation = entry.second.translation + delta;
        }
    }
}

void EditController::updateGizmoDrag(const Context& ctx, const lot_pick::Ray& ray) {
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
    if (snap_.valid()) {
        LOT_LOG("snap: " << (snap_.kind == lot_osnap::Kind::Endpoint ? "endpoint" : "midpoint")
                << " of object " << snap_.id << " (" << snap_.screenDistance << "px)");
    }
    const vec3 p = pivot(ctx.objects);
    LOT_LOG("drag: end, pivot at (" << p.x << ", " << p.y << ", " << p.z << ")");
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

    for (const auto& entry : ctx.objects) {
        const LotGameObject& obj = entry.second;
        if (!obj.model) continue;

        // 모델의 모든 정점을 화면에 투영해서 본다. 경계 상자 꼭짓점보다 정확하고
        // (회전한 상자의 꼭짓점은 실제 실루엣보다 크다) 정점 수천 개는 값싸다.
        const mat4 m = obj.transform.mat4Transform();
        bool any = false, all = true;
        for (const vec3& p : obj.model->getPositions()) {
            float px, py;
            const bool inside = ctx.camera.projectToScreen(transformPoint(m, p), ctx.width,
                                                           ctx.height, px, py)
                && px >= minX && px <= maxX && py >= minY && py <= maxY;
            if (inside) any = true;
            else all = false;
            if (crossing ? any : !all) break;  // 결론이 났으면 그만
        }
        if (crossing ? any : all) selection_.insert(entry.first);
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
        copy.model = src->model;        // 공유 - GPU 버퍼 복사 없음
        copy.material = src->material;  // 공유
        const id_t newId = copy.getId();
        objects.emplace(newId, std::move(copy));
        copies.insert(newId);
    }
    LOT_LOG("copy: " << copies.size() << " objects duplicated");
    selection_ = std::move(copies);
}

void EditController::deleteSelection(LotGameObject::Map& objects) {
    if (selection_.empty()) return;
    drag_.active = false;

    size_t removed = 0;
    for (id_t id : selection_) {
        removed += objects.erase(id);
    }
    LOT_LOG("delete: " << removed << " objects removed");
    selection_.clear();
}

void EditController::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    // 선택 상자 (OBB). 오브젝트 변환을 그대로 타서 회전하면 같이 돈다.
    for (id_t id : selection_) {
        const auto* obj = LotGameObject::find(ctx.objects, id);
        if (obj && obj->model) {
            lines.addTransformedBox(obj->model->boundsMin(), obj->model->boundsMax(),
                                    obj->transform.mat4Transform(), kSelectionColor);
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
