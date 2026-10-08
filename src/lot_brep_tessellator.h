#pragma once

// Mini B-Rep의 해석 형상을 렌더링용 삼각형 캐시로 변환한다 (웹 이식: 네이티브 lot_brep_tessellator 와 같은 규칙).
// 반환된 Builder는 GPU 자원을 소유하지 않으므로 테스트와 백그라운드 계산에서도 사용할 수 있다.

#include "lot_brep_shape.h"
#include "lot_model.h"

#include <optional>

namespace lot {

    struct BRepTessellationOptions {
        int curvedSegments{48};
        glm::vec3 color{0.7f, 0.7f, 0.75f};
    };

    // 유효하지 않은 형상, 삼각형화할 수 없는 프로파일이면 nullopt.
    // feature edge 는 LotModel 이 만들 때 스스로 만든다 (웹 LotModel 생성자).
    std::optional<LotModel::Builder> tessellateBRep(
        const LotBRepShape& shape,
        const BRepTessellationOptions& options = {});

} // namespace lot
