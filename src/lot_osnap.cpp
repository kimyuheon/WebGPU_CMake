#include "lot_osnap.h"
#include "line_render_system.h"
#include "lot_model.h"

#include <cmath>
#include <limits>

namespace lot_osnap {
namespace {

// 월드 점이 커서에서 몇 픽셀 떨어져 있나. 카메라 뒤면 무한대.
float screenDistance(const Query& q, const vec3& world) {
    float px, py;
    if (!q.camera->projectToScreen(world, q.width, q.height, px, py)) {
        return std::numeric_limits<float>::max();
    }
    const float dx = px - q.mouseX;
    const float dy = py - q.mouseY;
    return std::sqrt(dx * dx + dy * dy);
}

// 후보 하나를 대본다. 더 가까우면 best 를 갱신.
void consider(const Query& q, Kind kind, const vec3& worldPoint, LotGameObject::id_t id,
              Snap& best) {
    const float d = screenDistance(q, worldPoint);
    if (d > q.radiusPx) return;
    // 같은 거리면 끝점을 중점보다 우선한다 - CAD 에서 끝점이 더 '강한' 스냅이다
    if (d < best.screenDistance
        || (d == best.screenDistance && kind == Kind::Endpoint && best.kind == Kind::Midpoint)) {
        best.kind = kind;
        best.point = worldPoint;
        best.id = id;
        best.screenDistance = d;
    }
}

}  // namespace

Snap find(const Query& q, const lot_pick::Ray& ray, const LotGameObject::Map& objects) {
    Snap best;
    best.screenDistance = std::numeric_limits<float>::max();
    if (!q.camera) return best;

    // 1. 커서 아래 삼각형 (끌고 있는 오브젝트는 제외)
    lot_pick::Hit hit;
    float bestT = std::numeric_limits<float>::max();
    for (const auto& entry : objects) {
        if (q.isExcluded(entry.first)) continue;
        lot_pick::Hit h;
        if (lot_pick::intersectObjectPrecise(ray, entry.second, h) && h.t < bestT) {
            bestT = h.t;
            hit = h;
        }
    }

    if (hit.valid()) {
        const LotGameObject* obj = LotGameObject::find(objects, hit.id);
        vec3 a, b, c;
        if (obj && obj->model && obj->model->getTriangle(hit.triangle, a, b, c)) {
            const mat4 m = obj->transform.mat4Transform();
            const vec3 wa = transformPoint(m, a);
            const vec3 wb = transformPoint(m, b);
            const vec3 wc = transformPoint(m, c);
            consider(q, Kind::Endpoint, wa, hit.id, best);
            consider(q, Kind::Endpoint, wb, hit.id, best);
            consider(q, Kind::Endpoint, wc, hit.id, best);
            consider(q, Kind::Midpoint, (wa + wb) * 0.5f, hit.id, best);
            consider(q, Kind::Midpoint, (wb + wc) * 0.5f, hit.id, best);
            consider(q, Kind::Midpoint, (wc + wa) * 0.5f, hit.id, best);
        }
        if (best.valid()) return best;
    }

    // 2. 폴백: 커서가 메시 밖. 모든 정점을 화면에 투영해 가장 가까운 끝점.
    //    실루엣 바로 옆에서 꼭짓점을 집을 때 필요하다. 정점 수천 개를 프레임마다
    //    투영해도 마이크로초 단위다.
    for (const auto& entry : objects) {
        if (q.isExcluded(entry.first)) continue;
        const LotGameObject& obj = entry.second;
        if (!obj.model) continue;
        const mat4 m = obj.transform.mat4Transform();
        for (const vec3& p : obj.model->getPositions()) {
            consider(q, Kind::Endpoint, transformPoint(m, p), entry.first, best);
        }
    }
    return best;
}

void addMarker(LineRenderSystem& lines, const Snap& snap, const LotCamera& camera,
               float viewportHeight, float sizePx) {
    if (!snap.valid()) return;

    // 카메라를 향한 평면에 그린다 - 어느 각도에서 봐도 정사각형/정삼각형이다
    const float s = camera.worldPerPixel(snap.point, viewportHeight) * sizePx;
    const vec3 r = camera.getRight() * s;
    const vec3 u = camera.getDown() * -s;  // 화면 위쪽
    const vec3& p = snap.point;

    if (snap.kind == Kind::Endpoint) {
        const vec3 color{1.0f, 0.9f, 0.2f};
        const vec3 c0 = p - r - u, c1 = p + r - u, c2 = p + r + u, c3 = p - r + u;
        lines.addLine(c0, c1, color);
        lines.addLine(c1, c2, color);
        lines.addLine(c2, c3, color);
        lines.addLine(c3, c0, color);
    } else {
        const vec3 color{0.3f, 1.0f, 0.5f};
        const vec3 top = p + u * 1.15f;
        const vec3 bl = p - r - u * 0.85f;
        const vec3 br = p + r - u * 0.85f;
        lines.addLine(top, bl, color);
        lines.addLine(bl, br, color);
        lines.addLine(br, top, color);
    }
}

}  // namespace lot_osnap
