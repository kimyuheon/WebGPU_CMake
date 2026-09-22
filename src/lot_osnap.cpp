#include "lot_osnap.h"
#include "line_render_system.h"
#include "lot_model.h"

#include <cmath>
#include <limits>

namespace lot_osnap {

const char* kindName(Kind kind) {
    switch (kind) {
    case Kind::Endpoint: return "endpoint";
    case Kind::Midpoint: return "midpoint";
    case Kind::Center: return "center";
    case Kind::Intersection: return "intersection";
    case Kind::Perpendicular: return "perpendicular";
    default: return "none";
    }
}

namespace {

// 스케치 세그먼트 하나 (월드). 교차/수직 후보를 모을 때 쓴다.
struct Segment {
    vec3 a, b;
    LotGameObject::id_t id;
};

// 두 3D 선분의 최근접점 파라미터 (s 는 ab 위, t 는 cd 위, 둘 다 [0, 1] 로 자른다).
// 평행이면 false.
bool closestSegmentParams(const vec3& a, const vec3& b, const vec3& c, const vec3& d,
                          float& s, float& t) {
    const vec3 u = b - a, v = d - c, w = a - c;
    const float uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), uw = dot(u, w), vw = dot(v, w);
    const float den = uu * vv - uv * uv;
    if (den < 1e-12f * uu * vv || uu < 1e-12f || vv < 1e-12f) return false;
    s = (uv * vw - vv * uw) / den;
    t = (uu * vw - uv * uw) / den;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return true;
}

// 세그먼트가 화면에서 커서 근처를 지나가나 (후보 전처리 - 전부 짝지어 보지 않게)
bool segmentNearCursor(const Query& q, const vec3& a, const vec3& b, float radiusPx) {
    float ax, ay, bx, by;
    if (!q.camera->projectToScreen(a, q.width, q.height, ax, ay)) return false;
    if (!q.camera->projectToScreen(b, q.width, q.height, bx, by)) return false;
    return lot_pick::distancePointToSegment2D(q.mouseX, q.mouseY, ax, ay, bx, by) <= radiusPx;
}

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
    }

    // 1-2. 스케치 오브젝트의 끝점/중점. 선은 면이 없어 '커서 아래 삼각형'이 없으므로
    //      항상 전부 대본다. 메시 후보와 같은 best 를 두고 겨루므로 더 가까운 쪽이 이긴다.
    for (const auto& entry : objects) {
        if (q.isExcluded(entry.first)) continue;
        const LotGameObject& obj = entry.second;
        if (!obj.isSketch()) continue;
        const std::vector<vec3> pts = obj.worldPoints();
        const size_t n = pts.size();
        for (size_t i = 0; i < n; ++i) {
            consider(q, Kind::Endpoint, pts[i], entry.first, best);
        }
        const size_t segments = obj.closed ? n : n - 1;
        for (size_t i = 0; i < segments; ++i) {
            consider(q, Kind::Midpoint, (pts[i] + pts[(i + 1) % n]) * 0.5f, entry.first, best);
        }
        if (obj.hasCurve()) {
            // 원/호의 중심. 쪼갠 점들의 중점이 아니라 정의된 중심이다.
            consider(q, Kind::Center, transformPoint(obj.transform.mat4Transform(), obj.curve.center),
                     entry.first, best);
        }
    }

    // 1-3. 교차점 / 수직점. 커서 근처를 지나는 스케치 세그먼트만 모아 (보통 몇 개)
    //      짝지어 본다. 세그먼트 수천 개를 매 프레임 전부 짝지으면 느려진다.
    {
        std::vector<Segment> near;
        const float reach = q.radiusPx * 2.0f;
        for (const auto& entry : objects) {
            if (q.isExcluded(entry.first)) continue;
            const LotGameObject& obj = entry.second;
            if (!obj.isSketch()) continue;
            const std::vector<vec3> pts = obj.worldPoints();
            const size_t n = pts.size();
            const size_t segments = obj.closed ? n : n - 1;
            for (size_t i = 0; i < segments; ++i) {
                const vec3& a = pts[i];
                const vec3& b = pts[(i + 1) % n];
                if (segmentNearCursor(q, a, b, reach)) near.push_back(Segment{a, b, entry.first});
            }
        }

        // 교차: 두 세그먼트의 최근접점이 화면에서 한 픽셀 안에 붙어 있으면 교차로 본다
        // (같은 평면의 선끼리는 정확히 0, 살짝 어긋난 3D 선도 잡힌다).
        for (size_t i = 0; i < near.size(); ++i) {
            for (size_t j = i + 1; j < near.size(); ++j) {
                float s, t;
                if (!closestSegmentParams(near[i].a, near[i].b, near[j].a, near[j].b, s, t)) continue;
                // 세그먼트 끝에서 만나는 것은 끝점 스냅이 이미 잡는다 - 여기서는 내부 교차만
                if (s < 1e-4f || s > 1.0f - 1e-4f || t < 1e-4f || t > 1.0f - 1e-4f) continue;
                const vec3 p1 = near[i].a + (near[i].b - near[i].a) * s;
                const vec3 p2 = near[j].a + (near[j].b - near[j].a) * t;
                const vec3 gap = p1 - p2;
                const float tol = q.camera->worldPerPixel(p1, q.height) * 1.0f;
                if (dot(gap, gap) > tol * tol) continue;
                consider(q, Kind::Intersection, (p1 + p2) * 0.5f, near[i].id, best);
            }
        }

        // 수직: 기준점에서 세그먼트에 내린 수선의 발 (세그먼트 안에 떨어질 때만)
        if (q.fromPoint) {
            for (const Segment& sg : near) {
                const vec3 u = sg.b - sg.a;
                const float uu = dot(u, u);
                if (uu < 1e-12f) continue;
                const float s = dot(*q.fromPoint - sg.a, u) / uu;
                if (s <= 1e-4f || s >= 1.0f - 1e-4f) continue;
                consider(q, Kind::Perpendicular, sg.a + u * s, sg.id, best);
            }
        }
    }
    if (best.valid()) return best;

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

    if (snap.kind == Kind::Intersection) {
        // X
        const vec3 color{1.0f, 0.5f, 0.5f};
        lines.addLine(p - r - u, p + r + u, color);
        lines.addLine(p - r + u, p + r - u, color);
    } else if (snap.kind == Kind::Perpendicular) {
        // ⊥ : 밑변 + 세로선
        const vec3 color{0.6f, 0.8f, 1.0f};
        lines.addLine(p - r - u, p + r - u, color);
        lines.addLine(p - u, p + u, color);
    } else if (snap.kind == Kind::Center) {
        // 원 (8각형으로)
        const vec3 color{1.0f, 0.6f, 0.3f};
        vec3 prev = p + r;
        for (int i = 1; i <= 8; ++i) {
            const float t = 6.2831853f * static_cast<float>(i) / 8.0f;
            const vec3 cur = p + r * std::cos(t) + u * std::sin(t);
            lines.addLine(prev, cur, color);
            prev = cur;
        }
    } else if (snap.kind == Kind::Endpoint) {
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
