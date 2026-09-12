#include "lot_picking.h"
#include "lot_model.h"

#include <cmath>
#include <limits>

namespace lot_pick {

Ray screenToRay(const LotCamera& camera, float px, float py, float width, float height) {
    // 1. 픽셀 -> NDC. WebGPU 의 NDC 는 +Y 가 위라서 y 를 뒤집는다.
    const float ndcX = (px / width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (py / height) * 2.0f;

    // 2. NDC -> 뷰 공간 방향.
    //    clip.x = P00 * vx, clip.w = vz 이므로 ndc.x = P00 * vx / vz.
    //    vz = 1 로 두면 vx = ndc.x / P00. y 도 같다 (P11 이 음수라 뒤집힘도 처리된다).
    const mat4& proj = camera.getProjection();
    const vec3 viewDir{ndcX / proj.m[0][0], ndcY / proj.m[1][1], 1.0f};

    // 3. 뷰 -> 월드. 뷰 행렬의 회전은 직교라 전치가 역이다.
    //    행 u, v, w 가 각각 월드의 x/y/z 뷰 축이므로 world = u*vx + v*vy + w*vz.
    const mat4& view = camera.getView();
    const vec3 u{view.m[0][0], view.m[1][0], view.m[2][0]};
    const vec3 v{view.m[0][1], view.m[1][1], view.m[2][1]};
    const vec3 w{view.m[0][2], view.m[1][2], view.m[2][2]};

    Ray ray;
    ray.origin = camera.getPosition();
    ray.direction = normalize(u * viewDir.x + v * viewDir.y + w * viewDir.z);
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
