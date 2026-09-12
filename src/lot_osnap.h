#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_math.h"
#include "lot_picking.h"

#include <set>

class LineRenderSystem;

// 오브젝트 스냅 (CAD 의 osnap). 커서 근처의 '의미 있는 점'을 찾는다.
//
// 후보는 커서 아래 삼각형의 꼭짓점(끝점)과 모서리 중점이다. 그 삼각형이
// 정확히 커서 밑에 있는 면이므로 뒤에 가려진 정점이 잡힐 일이 없다.
// 커서가 메시 밖(실루엣 바로 옆)이면 삼각형이 없으므로, 그때는 모든 정점을
// 화면에 투영해 가장 가까운 것을 찾는다.
//
// 비교는 화면 픽셀 거리로 한다. 월드 거리로 하면 멀리 있는 작은 물체에서
// 스냅이 안 걸리고 가까운 큰 물체에서는 너무 잘 걸린다.
//
// Vulkan 쪽 osnap_render_system 과 같은 자리다.
namespace lot_osnap {

enum class Kind {
    None,
    Endpoint,  // 꼭짓점 - 마커는 사각형
    Midpoint,  // 모서리 중점 - 마커는 삼각형
};

struct Snap {
    Kind kind = Kind::None;
    vec3 point{};                 // 월드
    LotGameObject::id_t id = LotGameObject::kInvalidId;
    float screenDistance = 0.0f;  // 커서와의 픽셀 거리

    bool valid() const { return kind != Kind::None; }
};

struct Query {
    const LotCamera* camera = nullptr;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float radiusPx = 14.0f;
    // 이 오브젝트들은 후보에서 뺀다 - 끌고 있는 것들이 제 정점에 붙지 않도록.
    // nullptr 이면 아무것도 빼지 않는다.
    const std::set<LotGameObject::id_t>* exclude = nullptr;

    bool isExcluded(LotGameObject::id_t id) const {
        return exclude != nullptr && exclude->count(id) != 0;
    }
};

// 커서 아래를 정밀 피킹한 뒤 스냅 후보를 찾는다. 없으면 kind == None.
Snap find(const Query& query, const lot_pick::Ray& ray, const LotGameObject::Map& objects);

// 마커를 선 시스템에 그린다. 화면 크기가 일정하도록 카메라 거리로 스케일한다.
// 끝점 = 사각형, 중점 = 삼각형 (CAD 관례).
void addMarker(LineRenderSystem& lines, const Snap& snap, const LotCamera& camera,
               float viewportHeight, float sizePx);

}  // namespace lot_osnap
