#pragma once

#include <string>
#include <vector>

// UI 명령 하나 - 메뉴 항목이자 리본 단추.
//
// 명령은 스스로 아무 일도 하지 않는다. 누르면 그 명령이 들고 있는 '키'를 C++ 로
// 되돌려 보내고, 키보드 이벤트와 같은 경로로 처리된다 (main 의 lot_onToolbarKey).
// 그래서 메뉴/리본/단축키가 갈라질 수 없다 - 셋이 같은 표를 본다.
//
// Vulkan 쪽은 ImGui::MenuItem 이 ToolAction 을 돌려주고 FirstApp 이 그걸 처리하는데,
// 웹은 DOM 이 UI 를 그리므로 '표를 JSON 으로 내보내고 결과를 키로 받는' 모양이 된다.
namespace lot_ui {

struct Command {
    std::string id;        // "view.top" 처럼 고유. 테스트와 상태 표시가 이걸로 가리킨다
    std::string label;     // 화면에 보이는 이름 (한국어)
    std::string keyCode;   // KeyboardEvent.code. "@open" 처럼 @ 로 시작하면 JS 쪽 동작
    bool ctrl = false;     // Ctrl 조합인가
    std::string shortcut;  // 메뉴 오른쪽에 회색으로 붙는 글자 ("Ctrl+Z", "T")
    std::string tip;       // 툴팁
    std::string state;     // 켜짐 표시를 볼 상태 키 ("view:2", "ortho"). 비면 표시 없음
    std::string icon;      // 리본에 쓸 짧은 기호 (없으면 label 앞 글자)
    bool separatorBefore = false;
    // 상태바 단추의 위로 펼치는 메뉴 (객체스냅 설정, 비주얼 스타일). 비면 없음.
    std::string menuTitle;
    std::vector<Command> menu;
};

struct Group {
    std::string caption;            // 리본 그룹 이름 / 메뉴 제목
    std::vector<Command> commands;
};

// JSON 조각 만들기 도우미 (외부 라이브러리 없이 손으로 엮는다).
std::string quote(const std::string& s);
std::string commandJson(const Command& c);

}  // namespace lot_ui
