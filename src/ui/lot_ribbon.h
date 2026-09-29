#pragma once

#include "lot_ui_command.h"

#include <string>
#include <vector>

// 리본 - 메뉴바 바로 아래 가로 띠. 탭(홈 / 2D / 3D)마다 아이콘 그룹이 한 줄로 놓인다.
// Vulkan 쪽 LotRibbon 과 같은 자리다: 메뉴와 **같은 명령**을 아이콘으로 꺼내 둔 것이라
// 둘이 갈라질 수 없다 (표가 하나).
//
// 그룹마다 작은 캡션이 아래에 붙고, 켜진 명령은 파랗게 강조된다. 탭 줄을 더블클릭하면
// 본문이 접힌다 (DOM 쪽 lot_ui.js 가 처리).
namespace lot_ui {

class LotRibbon {
public:
    struct Tab {
        std::string name;
        std::vector<Group> groups;
    };

    LotRibbon();

    const std::vector<Tab>& tabs() const { return tabs_; }

    // [{tab, groups:[{caption, items:[...]}]}, ...]
    std::string toJson() const;

private:
    std::vector<Tab> tabs_;
};

}  // namespace lot_ui
