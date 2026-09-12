#include "lot_picking.h"
#include "lot_model.h"

#include <cmath>
#include <limits>

namespace lot_pick {

Ray screenToRay(const LotCamera& camera, float px, float py, float width, float height) {
    // 1. 픽셀 -> NDC. WebGPU 의 NDC 는 +Y 가 위라서 y 를 뒤집는다.
    const float ndcX = (px / width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (py / height) * 2.0f;

    // 뷰 -> 월드. 뷰 행렬의 회전은 직교라 전치가 역이다.
    // 행 u, v, w 가 각각 월드의 x/y/z 뷰 축이다.
    const mat4& proj = camera.getProjection();
    const mat4& view = camera.getView();
    const vec3 u{view.m[0][0], view.m[1][0], view.m[2][0]};
    const vec3 v{view.m[0][1], view.m[1][1], view.m[2][1]};
    const vec3 w{view.m[0][2], view.m[1][2], view.m[2][2]};

    Ray ray;
    if (camera.isOrthographic()) {
        // 직교: 레이는 전부 카메라 정면(w)과 평행이고, 시작점이 화면 위치에 따라 다르다.
        // ndc.x = P00 * vx + P30 이므로 vx = (ndc.x - P30) / P00. y 도 같다.
        const float vx = (ndcX - proj.m[3][0]) / proj.m[0][0];
        const float vy = (ndcY - proj.m[3][1]) / proj.m[1][1];
        ray.origin = camera.getPosition() + u * vx + v * vy;
        ray.direction = normalize(w);
    } else {
        // 원근: 레이는 전부 카메라 위치에서 나가고, 방향이 화면 위치에 따라 다르다.
        // clip.x = P00 * vx, clip.w = vz 이므로 ndc.x = P00 * vx / vz.
        // vz = 1 로 두면 vx = ndc.x / P00. y 도 같다 (P11 이 음수라 뒤집힘도 처리된다).
        const vec3 viewDir{ndcX / proj.m[0][0], ndcY / proj.m[1][1], 1.0f};
        ray.origin = camera.getPosition();
        ray.direction = normalize(u * viewDir.x + v * viewDir.y + w * viewDir.z);
    }
    return ray;
}

bool intersectObject(const Ray& ray, const LotGameObject& object, float& tOut) {
    if (!object.model) return false;

    // 레이를 오브젝트의 로컬 공간으로 가져오면 회전한 상자(OBB)가
    // 축 정렬 상자(AABB)가 되어 슬랩 테스트로 끝난다.
    // 방향은 정규화하지 않는다 - 그래야 t 가 월드에서의 t 와 같다
    // (변환이 선형이라 origin + t*dir 의 t 는 양쪽에서 같은 값이다).
    const vec3 o = object.transform.worldToLocalPoint(ray.origin);
    const vec3 d = object.transform.worldToLocalDirection(ray.direction);
    const vec3 lo = object.model->boundsMin();
    const vec3 hi = object.model->boundsMax();

    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    const float os[3] = {o.x, o.y, o.z};
    const float ds[3] = {d.x, d.y, d.z};
    const float los[3] = {lo.x, lo.y, lo.z};
    const float his[3] = {hi.x, hi.y, hi.z};

    for (int i = 0; i < 3; ++i) {
        if (std::fabs(ds[i]) < 1e-8f) {
            // 이 축과 평행 - 슬랩 안에 있어야만 통과
            if (os[i] < los[i] || os[i] > his[i]) return false;
            continue;
        }
        float t1 = (los[i] - os[i]) / ds[i];
        float t2 = (his[i] - os[i]) / ds[i];
        if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tMin) tMin = t1;
        if (t2 < tMax) tMax = t2;
        if (tMin > tMax) return false;
    }

    tOut = tMin;
    return true;
}

LotGameObject::id_t pickObject(const Ray& ray, const LotGameObject::Map& objects,
                               float& tOut) {
    LotGameObject::id_t best = LotGameObject::kInvalidId;
    float bestT = std::numeric_limits<float>::max();

    for (const auto& entry : objects) {
        float t = 0.0f;
        if (intersectObject(ray, entry.second, t) && t < bestT) {
            bestT = t;
            best = entry.first;
        }
    }

    tOut = bestT;
    return best;
}

bool intersectTriangle(const vec3& o, const vec3& d,
                       const vec3& a, const vec3& b, const vec3& c,
                       float& tOut, float& uOut, float& vOut) {
    // Möller-Trumbore: 레이를 삼각형의 무게중심 좌표계로 옮겨 푼다.
    //   o + t d = a + u (b - a) + v (c - a),  u >= 0, v >= 0, u + v <= 1
    // 행렬식 하나(det)로 세 미지수를 다 구하므로 삼각형마다 값싸다.
    const vec3 e1 = b - a;
    const vec3 e2 = c - a;
    const vec3 p = cross(d, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;  // 레이가 삼각형 평면과 평행

    const float invDet = 1.0f / det;
    const vec3 s = o - a;
    const float u = dot(s, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    const vec3 q = cross(s, e1);
    const float v = dot(d, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = dot(e2, q) * invDet;
    if (t < 0.0f) return false;  // 레이 뒤

    tOut = t;
    uOut = u;
    vOut = v;
    return true;
}

bool intersectObjectPrecise(const Ray& ray, const LotGameObject& object, Hit& hitOut) {
    if (!object.model) return false;

    // 1. 경계 상자로 먼저 거른다. 대부분의 클릭은 여기서 끝난다.
    float boxT = 0.0f;
    if (!intersectObject(ray, object, boxT)) return false;

    // 2. 로컬 공간에서 삼각형을 전부 돈다. 방향을 정규화하지 않으므로 t 가
    //    월드의 t 와 같다 (변환이 선형이라 origin + t*dir 의 t 는 양쪽에서 같다).
    const vec3 o = object.transform.worldToLocalPoint(ray.origin);
    const vec3 d = object.transform.worldToLocalDirection(ray.direction);
    const LotModel& model = *object.model;

    bool found = false;
    float bestT = std::numeric_limits<float>::max();
    size_t bestTri = 0;
    const size_t count = model.getTriangleCount();
    for (size_t i = 0; i < count; ++i) {
        vec3 a, b, c;
        if (!model.getTriangle(i, a, b, c)) break;
        float t, u, v;
        if (intersectTriangle(o, d, a, b, c, t, u, v) && t < bestT) {
            bestT = t;
            bestTri = i;
            found = true;
        }
    }
    if (!found) return false;

    hitOut.id = object.getId();
    hitOut.t = bestT;
    hitOut.point = ray.origin + ray.direction * bestT;
    hitOut.localPoint = o + d * bestT;
    hitOut.triangle = bestTri;
    return true;
}

Hit pickObjectPrecise(const Ray& ray, const LotGameObject::Map& objects) {
    Hit best;
    float bestT = std::numeric_limits<float>::max();
    for (const auto& entry : objects) {
        Hit hit;
        if (intersectObjectPrecise(ray, entry.second, hit) && hit.t < bestT) {
            bestT = hit.t;
            best = hit;
        }
    }
    return best;
}

bool closestPointOnLine(const Ray& ray, const vec3& a, const vec3& dir, float& sOut) {
    // 두 직선 P(t) = o + t d, L(s) = a + s e 사이 거리를 최소화한다.
    // d, e 가 단위 벡터일 때:
    //   t = (b (w.e) - (w.d)) / (1 - b^2),  s = t b + (w.e)
    // 여기서 w = o - a, b = d.e.  1 - b^2 이 0 이면 평행이다.
    const vec3 e = normalize(dir);
    const vec3 w = ray.origin - a;
    const float b = dot(ray.direction, e);
    const float denom = 1.0f - b * b;
    if (denom < 1e-6f) return false;

    const float t = (b * dot(w, e) - dot(w, ray.direction)) / denom;
    sOut = t * b + dot(w, e);
    return true;
}

float distanceRayToSegment(const Ray& ray, const vec3& a, const vec3& b, float& sOut) {
    const vec3 ab = b - a;
    const float len = std::sqrt(dot(ab, ab));
    if (len < 1e-8f) {
        sOut = 0.0f;
        const vec3 w = a - ray.origin;
        const vec3 perp = w - ray.direction * dot(w, ray.direction);
        return std::sqrt(dot(perp, perp));
    }

    float s = 0.0f;
    if (!closestPointOnLine(ray, a, ab, s)) {
        s = 0.0f;  // 평행이면 끝점으로 잰다
    }
    // 선분 밖으로 나가면 끝점에 붙인다
    if (s < 0.0f) s = 0.0f;
    if (s > len) s = len;

    const vec3 p = a + normalize(ab) * s;
    const vec3 w = p - ray.origin;
    const float along = dot(w, ray.direction);
    if (along < 0.0f) {
        // 카메라 뒤 - 거리는 그냥 크게
        sOut = s / len;
        return std::numeric_limits<float>::max();
    }
    const vec3 perp = w - ray.direction * along;
    sOut = s / len;
    return std::sqrt(dot(perp, perp));
}

}  // namespace lot_pick
