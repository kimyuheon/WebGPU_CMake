#pragma once

#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"

#include <string>
#include <vector>

class lot_web_device;

// 피처 치수 - 솔리드웍스 Instant3D 식 (네이티브 first_app/feature_dims.cpp): 솔리드 하나를 고르면 그것을 만든
// 치수가 모델 위에 뜨고, 고친 값으로 솔리드 / 스케치를 바꾼 뒤 다시 만든다.
//   무엇: 돌출 높이 · 보스 높이 · 스케치 치수 (원 Ø, 사각형 가로 · 세로, 다각형 변) · 원 컷 / 보스의 위치 (가장자리 둘까지)
//         · 상자 (가로 · 세로 · 높이) · 원기둥 (지름 · 높이).
//   계산은 한 곳 (collect) - 표시와 편집이 같은 목록 · 번호를 쓴다.
namespace lot_feature_dims {

struct Dim {
    enum class Kind { Width, Length, Height, Diameter, Edge, Location };
    Kind kind = Kind::Height;
    float value = 0.0f;
    std::string label;                          // "돌출 높이", "컷 1 Ø" ...
    unsigned sketch = FeatureLink::kNone;       // 스케치 치수면 그 스케치, 솔리드 매개변수면 kNone
    int axis = -1;                              // 상자 축 / 보스 높이 = 100 + i / 다각형 변 번호
    bool diameter = false;                      // p1 = 중심, p2 = 원 위의 점 (지시선 방향)
    vec3 p1{}, p2{}, dimLine{}, normal{0.0f, 0.0f, 1.0f};   // 월드. 정렬 치수의 측정점 둘 · 치수선 자리 · 평면
    int clippedSide = 0;                        // 컷 사각형이 솔리드 밖으로 나가 잘린 쪽 (1 먼 변, 2 가까운 변)
    vec3 locDir{};                              // 위치 치수 - 가장자리 -> 중심 (값을 늘리면 이쪽으로 민다)
};

// 솔리드 id 의 치수들. toCam = 솔리드에서 카메라 쪽 (치수 평면 앞면 · 글자 방향).
std::vector<Dim> collect(const LotGameObject::Map& objects, LotGameObject::id_t solid, const vec3& toCam);

// 치수 index 를 value 로 (스케치를 고치거나 솔리드 매개변수를 바꾸고 다시 만든다). 한 번의 실행 취소.
// 다시 만들다 새 오류가 나면 (구멍이 판 밖으로 나감 등) 되돌리고 false 와 why.
bool set(LotGameObject::Map& objects, LotGameObject::id_t solid, int index, float value, lot_web_device& device,
         EditHistory& history, std::string& why);

}  // namespace lot_feature_dims
