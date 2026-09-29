#pragma once

#include "lot_ui_command.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace lot_ui {

class LotMainMenu;
class LotRibbon;

// 명령행 - 화면 아래에 치는 입구. 메뉴 · 리본 · 단축키에 이은 네 번째지만 같은 표를 본다.
//
// Vulkan 쪽 builtinCommandTable 과 같은 규약이다: 영문 풀이름 · 짧은 별칭 · 한글을 모두
// 받는다 (line / l / 선). 이름을 치면 그 명령의 키 코드를 돌려주고, main 이 키보드와
// 같은 경로로 흘려보낸다.
//
// 숫자만 친 것은 명령이 아니라 '값'이다 (도구가 거리·각도·배율로 받는다) - 그건 여기서
// 판단하지 않고 호출자가 도구에 넘긴다.
class LotCommandLine {
public:
    struct Resolved {
        bool found = false;
        std::string id;        // "draw.line"
        std::string keyCode;   // 되돌려 보낼 키
        bool ctrl = false;
        std::string label;     // 로그에 쓸 이름
    };

    // 메뉴/리본의 명령을 모아 이름표를 만든다. 별칭은 아래 표에서 더한다.
    void build(const LotMainMenu& menu, const LotRibbon& ribbon);

    // 친 글자를 명령으로. 대소문자·앞뒤 공백은 무시한다.
    Resolved resolve(const std::string& typed) const;

    // 앞글자가 맞는 이름들 (최대 max 개). 입력창이 보여준다.
    std::vector<std::string> complete(const std::string& prefix, size_t max = 6) const;

    // 이름표 전체 (JSON 배열). 입력창이 자동완성에 쓴다.
    std::string namesJson() const;

private:
    void addCommand(const Command& c);

    std::unordered_map<std::string, Command> byName_;  // 소문자 이름 -> 명령
    std::vector<std::string> names_;                   // 보여줄 순서대로
};

}  // namespace lot_ui
