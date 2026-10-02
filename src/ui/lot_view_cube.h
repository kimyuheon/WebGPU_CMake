#pragma once

#include "lot_camera.h"

#include <string>

// 뷰큐브 - 오른쪽 위 모서리의 작은 상자. 지금 시점을 상자로 보여주고, 면·모서리·
// 꼭짓점을 누르면 그 방향에서 보는 뷰로 간다 (AutoCAD / Fusion 의 ViewCube).
//
// 네이티브(lot_view_cube.cpp)는 상자를 직접 투영해 삼각형으로 그리지만, 웹에서는
// 그릴 필요가 없다 - CSS 3D 변환이 상자를 돌려 주고, 면마다 올려둔 3x3 칸이
// 26 방향 판정을 대신한다. 뒤를 보는 면은 backface-visibility 가 알아서 숨긴다.
// 그래서 C++ 이 할 일은 둘뿐이다: 지금 자세를 넘기고, 눌린 방향을 받는다.
//
// 넘기는 것은 뷰 행렬의 회전부다. 우리 뷰 공간은 +X 오른쪽 · +Y 아래 · +Z 앞이라
// CSS 의 화면 축과 규약이 같다 - 그대로 matrix3d 로 쓸 수 있다.
namespace lot_ui {

class LotViewCube {
public:
    // 바뀌었을 때만 true. 같은 자세를 프레임마다 DOM 에 밀어 넣지 않는다.
    bool poll(const LotCamera& camera);

    // 마지막으로 읽은 자세, CSS matrix3d 인자 차례(열 우선) 16 개.
    const std::string& matrixCss() const { return matrix_; }

    // DOM 을 다시 만들어야 할 때 (설치 실패 등) - 다음 poll 이 반드시 넘기게 한다.
    void invalidate() { has_ = false; }

private:
    mat4 last_{};
    std::string matrix_;
    bool has_ = false;
};

}  // namespace lot_ui
