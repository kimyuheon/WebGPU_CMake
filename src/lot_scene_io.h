#pragma once

#include "lot_game_object.h"

#include <memory>
#include <string>

class lot_web_device;
class LotMaterial;

// .lot 씬 파일 (JSON) 저장 / 열기.
//
// Vulkan 쪽 scene_lot_io 와 같은 포맷을 쓴다 (format "lot", version 3). 그쪽이
// 저장한 파일의 line / polyline / mesh 는 여기서 열리고, 여기서 저장한 파일은
// 그쪽에서 열린다. 원/호/문자/치수/점군처럼 웹에 아직 없는 종류는 건너뛰고 센다.
//
// 좌표계가 다르다: 네이티브는 Z-up (CAD 표준), 웹은 아직 Vulkan 튜토리얼 규약
// (+Y 아래, 바닥 = XZ). 파일은 항상 네이티브 좌표로 쓰고, 읽고 쓸 때 여기서
// 한 번 돌린다 - 웹 (x, y, z) -> 네이티브 (x, z, -y). 이건 X 축 둘레 -90도
// 회전이라 위치/정점은 그대로 돌리고, 쿼터니언은 켤레 곱, 스케일은 성분을
// 자리바꿈한다. 나중에 웹이 Z-up 으로 옮기면 이 변환만 항등으로 바꾸면 된다.
namespace lot_scene {

struct LoadStats {
    int meshes = 0;
    int lines = 0;
    int polylines = 0;
    int skipped = 0;          // 지원하지 않는 종류
    std::string skippedKinds; // 로그용: "circle, text" 처럼
    std::string error;        // 비어 있지 않으면 실패
};

// 씬 전체를 .lot JSON 문자열로. model 이 없고 sketch 도 아닌 오브젝트
// (뷰어 오브젝트)는 건너뛴다.
std::string save(const LotGameObject::Map& objects);

// JSON 텍스트를 읽어 objects 에 추가한다 (기존 것은 지우지 않는다 - 호출자가
// 새 씬을 원하면 먼저 비운다). 메시는 GPU 버퍼를 만들어야 하므로 device 가 필요하고,
// 재질은 파일에 없으므로 defaultMaterial 을 붙인다 (nullptr 이면 렌더 기본값).
LoadStats load(const std::string& text, lot_web_device& device,
               std::shared_ptr<LotMaterial> defaultMaterial, LotGameObject::Map& objects);

}  // namespace lot_scene
