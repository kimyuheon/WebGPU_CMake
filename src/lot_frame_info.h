#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"

#include <webgpu/webgpu.h>

// 한 프레임을 그리는 데 필요한 것들을 한 묶음으로.
//
// 렌더 시스템이 하나일 때는 인자로 하나씩 넘겨도 됐지만, 둘 이상이 되면
// 같은 목록을 매번 반복하게 된다. 새 렌더 시스템은 이 구조체 하나만 받는다.
// Vulkan 쪽 lot_frame_info 와 같은 역할이다.
struct FrameInfo {
    float frameTime;
    WGPURenderPassEncoder pass;
    const LotCamera& camera;

    // @group(0). 카메라 + 조명. LotGlobalUniform 이 만든다.
    WGPUBindGroup globalBindGroup;

    LotGameObject::Map& gameObjects;
};
