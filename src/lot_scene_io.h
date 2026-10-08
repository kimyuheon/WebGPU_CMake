#pragma once

#include "lot_game_object.h"
#include "lot_layers.h"

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
// 좌표계는 둘 다 Z-up (CAD 표준) 이라 파일 좌표가 곧 월드 좌표다. 웹이 +Y 아래
// 규약이던 시절의 변환 함수(toNative/fromNative)는 항등으로 남겨 두었다.
namespace lot_scene {

struct LoadStats {
    int meshes = 0;
    int lines = 0;
    int polylines = 0;
    int circles = 0;
    int arcs = 0;
    int dimensions = 0;
    int texts = 0;
    int lights = 0;
    int hatches = 0;
    int solids = 0;          // meshes 중 해석 형상(brep)이 있는 것
    int featureLinks = 0;
    int layers = 0;
    int skipped = 0;          // 지원하지 않는 종류
    std::string skippedKinds; // 로그용: "circle, text" 처럼
    std::string error;        // 비어 있지 않으면 실패
};

// 씬 전체를 .lot JSON 문자열로. model 이 없고 sketch 도 아닌 오브젝트
// (뷰어 오브젝트)는 건너뛴다.
std::string save(const LotGameObject::Map& objects, const LotLayers& layers);

// JSON 텍스트를 읽어 objects 에 추가한다 (기존 것은 지우지 않는다 - 호출자가
// 새 씬을 원하면 먼저 비운다). 메시는 GPU 버퍼를 만들어야 하므로 device 가 필요하고,
// 재질은 파일에 없으므로 defaultMaterial 을 붙인다 (nullptr 이면 렌더 기본값).
// layers 는 파일의 층으로 덮어쓴다 (파일 id -> 새 id 로 매핑해 오브젝트에 반영).
LoadStats load(const std::string& text, lot_web_device& device,
               std::shared_ptr<LotMaterial> defaultMaterial, LotGameObject::Map& objects,
               LotLayers& layers);

}  // namespace lot_scene
