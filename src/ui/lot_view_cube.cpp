#include "lot_view_cube.h"

#include <cmath>
#include <cstdio>

namespace lot_ui {
namespace {

// 0 에 붙은 값이 "-0" 으로 나가면 CSS 가 받기는 하지만 쓸데없이 바뀐 것처럼 보인다.
std::string number(float v) {
    if (std::fabs(v) < 1e-6f) return "0";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.5f", static_cast<double>(v));
    return buf;
}

}  // namespace

bool LotViewCube::poll(const LotCamera& camera) {
    const mat4& view = camera.getView();

    // 회전부만 본다. 이동은 상자와 상관없다 (상자는 늘 같은 자리, 같은 크기).
    if (has_) {
        bool same = true;
        for (int c = 0; c < 3 && same; ++c) {
            for (int r = 0; r < 3; ++r) {
                if (std::fabs(view.m[c][r] - last_.m[c][r]) > 1e-4f) { same = false; break; }
            }
        }
        if (same) return false;
    }
    last_ = view;
    has_ = true;

    // CSS matrix3d 는 열 우선으로 열여섯 개를 받는다. 우리 mat4 도 열 우선(m[열][행])
    // 이라 회전부를 그대로 옮기고 이동은 0 으로 둔다.
    //
    // 한 가지만 뒤집는다: 우리 뷰 공간은 +Z 가 '앞'(화면 안쪽) 인데 CSS 의 +Z 는
    // 보는 사람 쪽이다. 셋째 행의 부호를 바꿔 그 차이를 흡수한다 - 안 그러면
    // 뒤통수가 보이고 backface-visibility 가 반대 면을 지운다.
    matrix_.clear();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!matrix_.empty()) matrix_ += ",";
            if (c < 3 && r < 3) {
                matrix_ += number((r == 2) ? -view.m[c][r] : view.m[c][r]);
            } else {
                matrix_ += (c == r) ? "1" : "0";
            }
        }
    }
    return true;
}

}  // namespace lot_ui
