#pragma once

#include "lot_edit_controller.h"
#include "lot_game_object.h"
#include "lot_layers.h"

#include <string>

// 속성 패널 - 선택한 것의 종류 · 층 · 색 · 선종류 · 위치와 종류별 값 (길이, 반지름, 문자 ...).
// Vulkan 쪽 속성 패널과 같은 자리다.
//
// 층 / 색 / 선종류 바꾸기는 레이어 패널 명령(LotLayerPanel::command)을 그대로 쓴다 - 둘이
// 같은 일을 다르게 하면 안 된다. 여기서 새로 하는 편집은 위치와 문자 내용/높이뿐이다.
namespace lot_ui {

class LotPropertyPanel {
public:
    // 바뀌었으면 JSON, 아니면 빈 문자열.
    std::string diffJson(const LotLayers& layers, const LotGameObject::Map& objects,
                         const EditController& edit);
    void invalidate() { last_.clear(); }

    // 패널에서 고친 값. key: "x" "y" "z" (선택 하나의 위치), "text", "textHeight".
    // 히스토리에 남기고, 바꿨으면 true.
    bool edit(const std::string& key, const std::string& value,
              LotGameObject::Map& objects, EditController& edit);

private:
    std::string last_;
};

}  // namespace lot_ui
