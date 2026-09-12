#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_math.h"

// 마우스 피킹 - 화면의 한 점에서 무엇을 골랐는지 알아내는 수학.
//
// 화면 좌표 -> 카메라에서 나가는 레이 -> 오브젝트와 교차 판정 순서다.
// GPU 를 쓰지 않고 CPU 에서 경계 상자로만 판정한다. 메시 삼각형 단위의
// 정밀한 피킹은 아니지만 CAD 의 선택 용도로는 상자면 충분하고, 무엇보다
// 같은 프레임 안에서 답이 나온다 (GPU 피킹은 한 프레임 늦다).
namespace lot_pick {

struct Ray {
    vec3 origin;
    vec3 direction;  // 단위 벡터
};

// 캔버스 픽셀 좌표를 월드 레이로.
// (px, py) 는 캔버스 왼쪽 위가 원점, width/height 는 캔버스 크기.
Ray screenToRay(const LotCamera& camera, float px, float py, float width, float height);

// 레이와 오브젝트(모델의 경계 상자를 오브젝트 변환으로 돌린 것)의 교차.
// 맞으면 true 와 레이 파라미터 t (origin + t * direction 이 교차점).
// 모델이 없는 오브젝트(카메라 뷰어 등)는 항상 false.
bool intersectObject(const Ray& ray, const LotGameObject& object, float& tOut);

// 레이가 처음 맞는 오브젝트 (경계 상자 기준). 없으면 kInvalidId.
// 빠르지만 거칠다 - 토러스의 구멍을 클릭해도 잡힌다. 후보를 거르는 데 쓴다.
LotGameObject::id_t pickObject(const Ray& ray, const LotGameObject::Map& objects,
                               float& tOut);

// 삼각형 단위 피킹 결과.
struct Hit {
    LotGameObject::id_t id = LotGameObject::kInvalidId;
    float t = 0.0f;         // origin + t * direction
    vec3 point{};           // 교점 (월드)
    vec3 localPoint{};      // 교점 (오브젝트 로컬) - 스냅이 로컬 정점과 비교할 때
    size_t triangle = 0;    // 모델 안에서의 삼각형 번호

    bool valid() const { return id != LotGameObject::kInvalidId; }
};

// 레이 vs 삼각형 (Möller-Trumbore). 맞으면 t 와 무게중심 좌표 (u, v).
// 방향이 단위 벡터가 아니어도 된다 - 그래야 로컬 공간에서 그대로 쓴다.
// 앞뒤를 가리지 않는다 (닫힌 메시라면 가장 가까운 교점이 어차피 앞면이다).
bool intersectTriangle(const vec3& origin, const vec3& direction,
                       const vec3& a, const vec3& b, const vec3& c,
                       float& tOut, float& uOut, float& vOut);

// 오브젝트의 삼각형을 전부 돌아 가장 가까운 교점. 경계 상자를 먼저 거른다.
bool intersectObjectPrecise(const Ray& ray, const LotGameObject& object, Hit& hitOut);

// 삼각형 단위로 가장 가까운 오브젝트. 경계 상자를 통과한 것만 삼각형을 돈다.
Hit pickObjectPrecise(const Ray& ray, const LotGameObject::Map& objects);

// 레이와 선분(a -> b) 사이의 가장 가까운 거리.
// 기즈모 축을 집을 때 쓴다. sOut 은 선분 위의 파라미터 (0 = a, 1 = b).
float distanceRayToSegment(const Ray& ray, const vec3& a, const vec3& b, float& sOut);

// 레이에 가장 가까운 '직선' (a + s * dir) 위의 파라미터 s.
// 기즈모를 끌 때 마우스가 축 위의 어디를 가리키는지 알아낸다.
// 레이와 직선이 평행하면 false.
bool closestPointOnLine(const Ray& ray, const vec3& a, const vec3& dir, float& sOut);

}  // namespace lot_pick
