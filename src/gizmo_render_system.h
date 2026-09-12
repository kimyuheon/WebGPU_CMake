#pragma once

#include "lot_dynamic_buffer.h"
#include "lot_frame_info.h"
#include "lot_math.h"
#include "lot_picking.h"
#include "lot_vertex.h"
#include "lot_web_pipeline.h"

#include <webgpu/webgpu.h>
#include <memory>
#include <vector>

class lot_web_device;

// 이동 기즈모 - 세 축 화살표 (X 빨강, Y 초록, Z 파랑).
//
// 일반 메시와 다른 점 둘:
//   1. 뎁스 테스트를 하지 않는다. 오브젝트 안에 파묻혀도 보여야 한다.
//   2. 카메라 거리에 비례해 키운다. 멀어져도 화면에서 같은 크기다.
//      (스케일 = 거리 * kScreenScale - 원근 나눗셈을 상쇄하는 셈)
//
// 지금은 그리기만 한다. 마우스로 집어서 끄는 것은 피킹이 들어와야 한다.
// Vulkan 쪽 gizmo_render_system 과 같은 자리다.
class GizmoRenderSystem {
public:
    GizmoRenderSystem() = default;
    ~GizmoRenderSystem();

    GizmoRenderSystem(const GizmoRenderSystem&) = delete;
    GizmoRenderSystem& operator=(const GizmoRenderSystem&) = delete;

    void create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);

    // position 에 기즈모를 그린다. 축은 월드 축이다 (이동 기즈모).
    // highlightAxis (0 = X, 1 = Y, 2 = Z) 는 밝게 그린다 - 끌고 있는 축 표시.
    void render(FrameInfo& frame, const vec3& position, int highlightAxis = -1);

    bool isReady() const;

    // 축 방향. 이동 기즈모는 월드 축 그대로다.
    static vec3 axisDirection(int axis);

    // 이 카메라 거리에서 화살표 길이. render 와 hitTestAxis 가 같은 값을 쓴다.
    float arrowLength(const LotCamera& camera, const vec3& position) const;

    // 레이가 어느 축 화살표를 집었는지. 없으면 -1.
    // 여러 축에 가까우면 레이에 가장 가까운 쪽.
    int hitTestAxis(const lot_pick::Ray& ray, const LotCamera& camera,
                    const vec3& position) const;

    // 화면에서 차지하는 크기. 카메라 거리 1 당 월드 단위 길이.
    float screenScale = 0.18f;

    // 축을 집었다고 볼 거리 (화살표 길이에 대한 비율).
    // 머리 반지름(0.08)보다 조금 넉넉하게.
    float pickTolerance = 0.14f;

private:
    void buildArrow(const vec3& origin, const vec3& axis, float length, const vec3& color);

    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<LotDynamicBuffer> buffer_;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    lot_web_device* device_ = nullptr;

    std::vector<Vertex> vertices_;
};
