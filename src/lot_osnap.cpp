#include "lot_osnap.h"
#include "line_render_system.h"
#include "lot_dimension.h"
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
    case Kind::FaceCenter: return "face center";
    case Kind::Node: return "node";
    case Kind::Quadrant: return "quadrant";
    case Kind::Tangent: return "tangent";
    case Kind::Nearest: return "nearest";
    default: return "none";
    }
}

const char* kindLabel(Kind kind) {
    switch (kind) {
    case Kind::Endpoint: return "끝점";
    case Kind::Midpoint: return "중간점";
    case Kind::Center: return "중심점";
    case Kind::FaceCenter: return "면 중심";
    case Kind::Node: return "노드";
    case Kind::Quadrant: return "사분점";
    case Kind::Intersection: return "교차점";
    case Kind::Perpendicular: return "수직점";
    case Kind::Tangent: return "접선";
    case Kind::Nearest: return "근처점";
    default: return "";
    }
}

unsigned& enabledKinds() {
    // 근처점만 끈 채로 시작한다 - 켜 두면 선 위 아무 데나 붙어 다른 스냅을 가린다
    static unsigned mask = kindBit(Kind::Endpoint) | kindBit(Kind::Midpoint) | kindBit(Kind::Center)
                         | kindBit(Kind::FaceCenter) | kindBit(Kind::Node) | kindBit(Kind::Quadrant)
                         | kindBit(Kind::Intersection) | kindBit(Kind::Perpendicular)
                         | kindBit(Kind::Tangent);
    return mask;
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
    if (!isEnabled(kind)) return;
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
        if (q.isExcluded(entry.first) || !lot_pick::isSelectable(entry.second)) continue;
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

            // 면 중심: 맞은 삼각형과 같은 평면의 삼각형들을 모아 그 정점들의 평균.
            // 사각 면은 삼각형 둘이라, 삼각형 하나의 무게중심으로는 면 가운데가 안 나온다.
            if (isEnabled(Kind::FaceCenter)) {
                const vec3 n0 = normalize(cross(b - a, c - a));
                const float size = std::sqrt(dot(b - a, b - a)) + 1e-6f;
                vec3 sum{0.0f, 0.0f, 0.0f};
                int count = 0;
                const size_t triangles = obj->model->getTriangleCount();
                for (size_t i = 0; i < triangles; ++i) {
                    vec3 p0, p1, p2;
                    if (!obj->model->getTriangle(i, p0, p1, p2)) continue;
                    const vec3 n = cross(p1 - p0, p2 - p0);
                    const float nl = std::sqrt(dot(n, n));
                    if (nl < 1e-12f || dot(n, n0) / nl < 0.999f) continue;
                    if (std::fabs(dot(n0, p0 - a)) > size * 1e-3f) continue;
                    sum = sum + p0 + p1 + p2;
                    count += 3;
                }
                if (count > 0) {
                    consider(q, Kind::FaceCenter, transformPoint(m, sum * (1.0f / static_cast<float>(count))),
                             hit.id, best);
                }
            }
        }
    }

    // 1-2. 스케치 오브젝트의 끝점/중점. 선은 면이 없어 '커서 아래 삼각형'이 없으므로
    //      항상 전부 대본다. 메시 후보와 같은 best 를 두고 겨루므로 더 가까운 쪽이 이긴다.
    for (const auto& entry : objects) {
        if (q.isExcluded(entry.first) || !lot_pick::isSelectable(entry.second)) continue;
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
            const mat4 m = obj.transform.mat4Transform();
            const LotGameObject::Curve& cv = obj.curve;
            const vec3 center = transformPoint(m, cv.center);
            consider(q, Kind::Center, center, entry.first, best);

            // 곡선 평면의 두 축 (월드). 반지름은 축척을 탄 길이로.
            const vec3 ax = transformPoint(m, cv.center + cv.right) - center;
            const vec3 ay = transformPoint(m, cv.center + cv.up) - center;
            const float r = cv.radius * std::sqrt(dot(ax, ax));
            const vec3 ux = normalize(ax), uy = normalize(ay);
            constexpr float kTwoPi = 6.2831853f;
            // 호 범위 안의 각인가 (원은 늘)
            auto onCurve = [&](float t) {
                if (cv.kind != LotGameObject::Curve::Kind::Arc) return true;
                float lo = cv.start, hi = cv.end;
                if (hi < lo) std::swap(lo, hi);
                while (t < lo) t += kTwoPi;
                while (t > lo + kTwoPi) t -= kTwoPi;
                return t <= hi + 1e-4f;
            };
            for (int k = 0; k < 4; ++k) {
                const float t = kTwoPi * 0.25f * static_cast<float>(k);
                if (onCurve(t)) consider(q, Kind::Quadrant, center + (ux * std::cos(t) + uy * std::sin(t)) * r, entry.first, best);
            }
            // 접선: 기준점에서 원에 그은 두 접선의 접점
            if (q.fromPoint && isEnabled(Kind::Tangent)) {
                const vec3 d = *q.fromPoint - center;
                const float dx = dot(d, ux), dy = dot(d, uy);
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > r * 1.0001f) {
                    const float base = std::atan2(dy, dx);
                    const float alpha = std::acos(r / dist);
                    for (float t : {base + alpha, base - alpha}) {
                        if (onCurve(t)) consider(q, Kind::Tangent, center + (ux * std::cos(t) + uy * std::sin(t)) * r, entry.first, best);
                    }
                }
            }
        }
    }

    // 1-2a. 노드: 점 객체 (문자 기준점, 광원)
    if (isEnabled(Kind::Node)) {
        for (const auto& entry : objects) {
            if (q.isExcluded(entry.first) || !lot_pick::isSelectable(entry.second)) continue;
            const LotGameObject& obj = entry.second;
            if (obj.isText() || obj.isLight()) consider(q, Kind::Node, obj.transform.translation, entry.first, best);
        }
    }

    // 1-2b. 치수의 측정점 / 치수선 끝 - 치수에 이어 치수를 달 때 필요하다
    for (const auto& entry : objects) {
        if (q.isExcluded(entry.first) || !lot_pick::isSelectable(entry.second)) continue;
        if (!entry.second.isDimension()) continue;
        for (const vec3& p : lot_dim::outlinePoints(entry.second)) {
            consider(q, Kind::Endpoint, p, entry.first, best);
        }
    }

    // 1-3. 교차점 / 수직점. 커서 근처를 지나는 스케치 세그먼트만 모아 (보통 몇 개)
    //      짝지어 본다. 세그먼트 수천 개를 매 프레임 전부 짝지으면 느려진다.
    {
        std::vector<Segment> near;
        const float reach = q.radiusPx * 2.0f;
        for (const auto& entry : objects) {
            if (q.isExcluded(entry.first) || !lot_pick::isSelectable(entry.second)) continue;
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

        // 근처점: 다른 스냅이 하나도 없을 때만, 커서에 가장 가까운 선 위의 점
        if (!best.valid() && isEnabled(Kind::Nearest)) {
            for (const Segment& sg : near) {
                float ax, ay, bx, by;
                if (!q.camera->projectToScreen(sg.a, q.width, q.height, ax, ay)) continue;
                if (!q.camera->projectToScreen(sg.b, q.width, q.height, bx, by)) continue;
                const float ex = bx - ax, ey = by - ay;
                const float ee = ex * ex + ey * ey;
                float t = (ee > 1e-9f) ? ((q.mouseX - ax) * ex + (q.mouseY - ay) * ey) / ee : 0.0f;
                t = std::fmin(std::fmax(t, 0.0f), 1.0f);
                consider(q, Kind::Nearest, sg.a + (sg.b - sg.a) * t, sg.id, best);
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
        if (q.isExcluded(entry.first) || !lot_pick::isSelectable(entry.second)) continue;
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
    } else if (snap.kind == Kind::FaceCenter) {
        // 사각형 + 가운데 점
        const vec3 color{1.0f, 0.8f, 0.4f};
        lines.addLine(p - r - u, p + r - u, color);
        lines.addLine(p + r - u, p + r + u, color);
        lines.addLine(p + r + u, p - r + u, color);
        lines.addLine(p - r + u, p - r - u, color);
        lines.addLine(p - r * 0.15f, p + r * 0.15f, color);
        lines.addLine(p - u * 0.15f, p + u * 0.15f, color);
    } else if (snap.kind == Kind::Node || snap.kind == Kind::Tangent) {
        // 원 + (노드는 X, 접선은 윗선)
        const vec3 color = snap.kind == Kind::Node ? vec3{1.0f, 1.0f, 1.0f} : vec3{0.6f, 1.0f, 0.9f};
        vec3 prev = p + r;
        for (int i = 1; i <= 12; ++i) {
            const float t = 6.2831853f * static_cast<float>(i) / 12.0f;
            const vec3 cur = p + r * std::cos(t) + u * std::sin(t);
            lines.addLine(prev, cur, color);
            prev = cur;
        }
        if (snap.kind == Kind::Node) {
            lines.addLine(p - (r + u) * 0.7f, p + (r + u) * 0.7f, color);
            lines.addLine(p - (r - u) * 0.7f, p + (r - u) * 0.7f, color);
        } else {
            lines.addLine(p - r * 1.2f + u, p + r * 1.2f + u, color);
        }
    } else if (snap.kind == Kind::Quadrant) {
        const vec3 color{1.0f, 0.6f, 0.9f};
        lines.addLine(p + u, p + r, color);
        lines.addLine(p + r, p - u, color);
        lines.addLine(p - u, p - r, color);
        lines.addLine(p - r, p + u, color);
    } else if (snap.kind == Kind::Nearest) {
        // 모래시계
        const vec3 color{0.8f, 0.8f, 0.8f};
        lines.addLine(p - r + u, p + r + u, color);
        lines.addLine(p + r + u, p - r - u, color);
        lines.addLine(p - r - u, p + r - u, color);
        lines.addLine(p + r - u, p - r + u, color);
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
