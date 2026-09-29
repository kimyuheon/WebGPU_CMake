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

    // 이 오브젝트를 그리나 (층이 꺼졌으면 아니다). main 이 채운다 - 렌더 시스템은
    // 층 표를 모르고 이 함수만 묻는다. 비워두면 전부 그린다.
    bool (*visibleFilter)(const LotGameObject&) = nullptr;

    // 이 오브젝트가 낼 색 ('층 따름'이면 층 색). main 이 채운다.
    vec3 (*colorFilter)(const LotGameObject&) = nullptr;

    bool isVisible(const LotGameObject& obj) const {
        return visibleFilter == nullptr || visibleFilter(obj);
    }
    vec3 colorOf(const LotGameObject& obj) const {
        return colorFilter == nullptr ? obj.color : colorFilter(obj);
    }
};
