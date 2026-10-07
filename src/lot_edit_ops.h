#pragma once

#include "lot_game_object.h"
#include "lot_history.h"

#include <set>
#include <string>

// 클릭 없이 선택에 바로 하는 편집 (네이티브 first_app/modify.cpp 의 applyExplode / applyJoin).
namespace lot_edit_ops {

// 분해: 폴리선(사각형 · 다각형 포함)을 변마다 선으로. 닫힌 것은 마지막-첫 변도. 길이 0 인 변은 버린다.
// 원 · 호 · 선 · 문자 · 메시는 그대로 둔다. 한 번의 실행 취소. 만든 선들의 id.
std::set<LotGameObject::id_t> explode(LotGameObject::Map& objects, const std::set<LotGameObject::id_t>& selection,
                                      EditHistory& history);

// 결합: 끝점이 맞닿은(허용오차 1e-3) 선 · 열린 폴리선들을 하나의 폴리선으로. 양 끝이 만나면 닫힌다.
// 이어지지 않는 조각이 하나라도 있으면 아무것도 바꾸지 않고 why 에 이유. 만든 폴리선 id (실패면 kInvalidId).
LotGameObject::id_t join(LotGameObject::Map& objects, const std::set<LotGameObject::id_t>& selection,
                         EditHistory& history, std::string& why);

}  // namespace lot_edit_ops
