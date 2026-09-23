#pragma once

#include "lot_math.h"

#include <cstdint>
#include <string>
#include <vector>

class LineRenderSystem;

// 선종류 (실선 · 파선 · 숨은선 · 중심선 …).
//
// AutoCAD 의 .lin 규칙 그대로: 양수 = 선, 음수 = 빈칸, 0 = 점. 단위는 도면 단위.
// 표준 8종의 id 는 네이티브(lot_linetype.h)와 같아야 한다 - .lot 에 id 로 적히기 때문이다.
//
// 어디에 붙나:
//   · 도면층 (LotLayers::Layer::linetype) - 기본 Continuous(0)
//   · 오브젝트 (LotGameObject::linetype) - 기본 ByLayer, 개별 지정도 가능
//   · 전역 축척 (linetypeScale, AutoCAD 의 LTSCALE)
//
// 네이티브는 두께 있는 선 셰이더가 누적 길이로 판정하지만, 웹은 선이 1px 라
// CPU 에서 무늬대로 잘라 선분으로 내보낸다 (프레임마다 다시 만드는 구조라 값싸다).
namespace lot_linetype {

constexpr uint32_t kContinuous = 0;
constexpr uint32_t kByLayer = 0xFFFFFFFFu;

struct Definition {
    uint32_t id;
    const char* name;
    const char* sample;        // 패널에 보여줄 모양
    std::vector<float> pattern;
};

// 표준 8종 + Continuous. id 순.
const std::vector<Definition>& standard();

// 없으면 nullptr (Continuous 포함 - 무늬가 없다).
const Definition* find(uint32_t id);
const char* name(uint32_t id);

// 점들을 무늬대로 잘라 선 시스템에 넣는다. Continuous 면 통째로 한 번.
// scale 은 전역 LTSCALE - 무늬 길이에 곱한다. 화면에서 무늬가 너무 잘아지지 않게
// minWorldPerDash 보다 짧아지는 무늬는 실선으로 떨어뜨린다 (줌 아웃했을 때).
void emit(LineRenderSystem& lines, const std::vector<vec3>& points, bool closed,
          const vec3& color, uint32_t linetype, float scale, float minDashWorld = 0.0f);

}  // namespace lot_linetype
