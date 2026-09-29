#pragma once

#include "lot_edit_controller.h"
#include "lot_game_object.h"
#include "lot_layers.h"

#include <string>

// 레이어 패널 - 층 목록과 선택 속성(색 · 선종류)을 보여주고 고치는 곳.
// Vulkan 쪽 lot_layer_panel 과 같은 자리다.
//
// 패널은 상태를 들고 있지 않는다: 층 표와 선택을 보고 JSON 을 만들어 DOM 에 밀고,
// DOM 이 돌려준 명령을 그 자리에서 적용한다. 편집은 히스토리에 남는다.
namespace lot_ui {

class LotLayerPanel {
public:
    // 지금 상태를 JSON 으로. 이전과 같으면 빈 문자열 (DOM 을 건드릴 필요가 없다).
    std::string diffJson(const LotLayers& layers, const LotGameObject::Map& objects,
                         const EditController& edit);

    // 다음 diffJson 이 반드시 내보내게 (DOM 이 아직 없어 놓친 경우).
    void invalidate() { last_.clear(); }

    // 패널에서 온 명령. action 은 lot_ui.cpp 의 표 참고.
    // 층/오브젝트를 바꾸고, 바꿨으면 true.
    bool command(const std::string& action, uint32_t id, int value,
                 LotLayers& layers, LotGameObject::Map& objects, EditController& edit);

private:
    std::string last_;
};

}  // namespace lot_ui
