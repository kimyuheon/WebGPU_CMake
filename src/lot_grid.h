#pragma once

#include "lot_math.h"

class LineRenderSystem;
class LotCamera;

// 바닥 격자 (Z-up 이므로 z = 0 의 XY 평면).
//
// 예전에는 만들 때 한 번 구운 모델이었는데 (GridRenderSystem), 간격이 0.5 로 고정이라
// 그리드 스냅(F9) 간격과 어긋났다. 이제 프레임마다 선으로 그린다 - 선 몇 십 개라
// 값싸고, 간격을 스냅과 맞출 수 있으며, 카메라를 따라다녀 줌 아웃해도 화면을 덮는다.
//
// 굵기 단계는 CAD 관례를 따른다: 눈금선(은은하게) / 다섯 칸마다 굵은 선 / 축선(X 빨강, Y 초록).
namespace lot_grid {

// spacing 은 눈금 간격 (0 이하면 그리지 않는다). 카메라가 보는 범위를 덮도록
// 알아서 넓힌다. viewportHeight 는 화면에서 너무 잘아지는 격자를 솎아내는 데 쓴다.
void draw(LineRenderSystem& lines, const LotCamera& camera, float spacing, float viewportHeight);

}  // namespace lot_grid
