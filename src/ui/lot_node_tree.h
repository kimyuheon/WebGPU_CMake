#pragma once

#include "lot_edit_controller.h"
#include "lot_game_object.h"
#include "lot_layers.h"

#include <string>
#include <unordered_map>
#include <vector>

// 노드 트리 - 씬 > 층 > 객체. Vulkan 쪽 노드 트리 패널과 같은 자리다.
//
// 큰 도면은 객체가 수만 개라 한 번에 DOM 으로 내보낼 수 없다. 그래서 둘로 나눈다:
//   - 요약 (summaryJson): 층마다 개수 / 숨긴 개수, 선택된 id. 바뀔 때만 DOM 으로 민다.
//   - 자식 (childrenJson): DOM 이 층을 펼칠 때 offset/limit 만큼 끌어간다.
// 층별 개수와 정렬된 id 목록은 편집 revision · 객체 수 · 숨김 버전이 바뀔 때만 다시 만든다.
namespace lot_ui {

class LotNodeTree {
public:
    // 바뀌었으면 JSON, 아니면 빈 문자열.
    std::string summaryJson(const LotLayers& layers, const LotGameObject::Map& objects,
                            const EditController& edit);

    // 층 하나의 객체들 (id 순). {"layer":id,"offset":n,"total":n,"items":[{id,label,hidden,selected}]}
    std::string childrenJson(uint32_t layerId, int offset, int limit, const LotGameObject::Map& objects,
                             const EditController& edit);

    // 숨김을 바꾼다. id 가 kInvalidId 면 전부. 숨긴 것은 선택에서 뺀다.
    void setHidden(LotGameObject::id_t id, bool hidden, LotGameObject::Map& objects, EditController& edit);

    // 다음 summaryJson 이 반드시 내보내게.
    void invalidate() { last_.clear(); }

    // 층별 객체 수 (레이어 패널도 쓴다). 캐시가 낡았으면 다시 센다.
    const std::unordered_map<uint32_t, int>& layerCounts(const LotGameObject::Map& objects,
                                                         const EditController& edit);

    // 트리에 보일 객체 이름 ("선 #12", "문자 '...'")
    static std::string label(const LotGameObject& obj, LotGameObject::id_t id);

private:
    void refresh(const LotGameObject::Map& objects, const EditController& edit);

    const LotGameObject::Map* objects_ = nullptr;
    uint64_t revision_ = ~0ull;
    size_t count_ = 0;
    uint64_t hiddenVersion_ = 0;      // setHidden 마다 +1
    uint64_t builtHiddenVersion_ = ~0ull;
    std::unordered_map<uint32_t, int> counts_;        // 층 -> 객체 수
    std::unordered_map<uint32_t, int> hiddenCounts_;  // 층 -> 숨긴 수
    std::unordered_map<uint32_t, std::vector<LotGameObject::id_t>> ids_;  // 층 -> id (정렬)
    std::string last_;
};

}  // namespace lot_ui
