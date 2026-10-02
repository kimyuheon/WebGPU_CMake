#pragma once

#include "lot_ui_command.h"

#include <string>
#include <vector>

// 주 메뉴 (창 맨 위 가로 띠). Vulkan 쪽 LotMainMenu 와 같은 자리, 같은 이름들이다:
//   파일 · 편집 · 뷰 · 그리기 · 수정 · 설정
//
// 여기는 '무엇이 있는가' 만 안다. 누르면 무슨 일이 일어나는지는 명령이 들고 있는
// 키가 정하고 (Command::keyCode), 그리는 것은 DOM 쪽(src/js/lot_ui.js)이 한다.
namespace lot_ui {

class LotMainMenu {
public:
    LotMainMenu();

    const std::vector<Group>& menus() const { return menus_; }

    // [{title, items:[...]}, ...]
    std::string toJson() const;

    // 하단 상태바 토글들 (메뉴 명령의 짧은 이름 사본). [{...command}, ...]
    std::string statusBarJson() const;

private:
    std::vector<Group> menus_;
    std::vector<Command> statusBar_;
};

}  // namespace lot_ui
