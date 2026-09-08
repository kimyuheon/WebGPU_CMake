#pragma once

#include "lot_math.h"

// 위치가 있는 광원. 방향 광원과 달리 거리에 따라 어두워진다.
struct PointLight {
    vec3 position{0.0f, 0.0f, 0.0f};
    vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

// 한 프레임의 조명 상태.
//
// 광원이 늘어나면 여기에 배열이 붙는다. 지금은 하나뿐이지만,
// 오브젝트별 유니폼이 아니라 프레임당 유니폼으로 따로 두는 이유가 이것이다.
struct SceneLighting {
    // 어느 면도 완전히 검지 않도록 바닥을 깔아주는 값.
    vec3 ambientColor{1.0f, 1.0f, 1.0f};
    float ambientIntensity = 0.02f;

    PointLight pointLight{};
};
