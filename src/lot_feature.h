#pragma once

#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"

#include <memory>
#include <string>
#include <vector>

class LotModel;
class lot_web_device;

// 솔리드 피처 (돌출 · 컷 · 보스) - 네이티브 first_app/scene.cpp · features.cpp · extrude_manager.cpp 의
// 같은 이름 흐름을 옮긴 것. 형상은 lot::LotBRepShape (불변), 화면 메시는 거기서 다시 나눈 것.
namespace lot_feature {

// 닫힌 스케치 (닫힌 폴리선 · 사각형 · 다각형 · 원) -> 월드 점, 평면 법선, 중심.
// 법선은 그린 방향과 무관하게 고정한다: 수평이면 +Z, 수직이면 +Y, 그 외 +X (네이티브 extractSketchInfo).
bool sketchProfile(const LotGameObject& src, std::vector<vec3>& pts, vec3& normal, vec3& center);

// 변환의 역행렬 (월드 -> 로컬). 회전 · 축척 · 이동만 있는 변환.
mat4 inverseOf(const TransformComponent& t);

// 솔리드 화면 메시. 실패하면 nullptr.
std::shared_ptr<LotModel> buildSolidModel(lot_web_device& device, const lot::LotBRepShape& shape);

// 스케치 하나를 법선 방향 height 만큼 (음수면 반대로) 돌출한 새 솔리드를 objects 에 넣는다.
// 원본 스케치는 남고 피처 연결로 묶인다. 만들지 못하면 kInvalidId 와 why.
LotGameObject::id_t extrude(LotGameObject::Map& objects, LotGameObject::id_t sketch, float height,
                            lot_web_device& device, std::string& why);

// 솔리드에 스케치로 컷 (through 면 관통, 아니면 위 캡에서 depth 만큼 포켓) / 보스 (스케치가 가까운 캡에서
// height 만큼 밖으로). 한 번의 실행 취소. 실패하면 false 와 why (솔리드는 그대로).
bool cut(LotGameObject::Map& objects, LotGameObject::id_t solid, LotGameObject::id_t sketch, float depth,
         bool through, lot_web_device& device, EditHistory& history, std::string& why);
bool boss(LotGameObject::Map& objects, LotGameObject::id_t solid, LotGameObject::id_t sketch, float height,
          lot_web_device& device, EditHistory& history, std::string& why);

// 돌출 높이 바꾸기 (관통 컷은 새 높이까지 관통). 한 번의 실행 취소.
bool setHeight(LotGameObject::Map& objects, LotGameObject::id_t solid, float height, lot_web_device& device,
               EditHistory& history, std::string& why);

// 보스 i 의 높이 (+ 위 캡에서 위로, - 아래 캡에서 아래로). 한 번의 실행 취소.
bool setBossHeight(LotGameObject::Map& objects, LotGameObject::id_t solid, unsigned boss, float height,
                   lot_web_device& device, EditHistory& history, std::string& why);

// 컷 · 보스 지우기 (네이티브 removeBRepFeatures). 번호는 feature().cuts / bosses 순서. 그 피처만 쓰던 스케치도
// 함께 지운다 (다른 솔리드가 쓰면 남긴다). 한 번의 실행 취소. 남은 것으로 모양을 못 만들면 false 와 why.
bool removeFeatures(LotGameObject::Map& objects, LotGameObject::id_t solid, std::vector<unsigned> cutIdx,
                    std::vector<unsigned> bossIdx, lot_web_device& device, EditHistory& history, std::string& why);

// 노드 트리의 피처 줄 (네이티브 LotSceneTreePanel::buildTree): 스케치 · 돌출 높이 · 보스 · 컷 · 오류.
// tag = "sketch" / "extrude" / "boss<i>" / "cut<i>" / "error", ref = 원본 스케치 id (없으면 kNone).
struct TreeRow {
    std::string tag;
    std::string label;
    unsigned ref = FeatureLink::kNone;
};
std::vector<TreeRow> treeRows(const LotGameObject& solid);

// 연결된 스케치가 바뀐 솔리드를 다시 만든다 (네이티브 updateFeatureRegeneration). 같은 id 를 지키고
// 히스토리에는 남기지 않는다 - 스케치 편집을 되돌리면 다음 번에 다시 맞춰진다. 다시 만든 개수.
int regenerate(LotGameObject::Map& objects, lot_web_device& device);

}  // namespace lot_feature
