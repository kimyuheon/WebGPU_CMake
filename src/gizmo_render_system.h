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

// 이동 기즈모 - 세 축 화살표 (X 빨강, Y 초록, Z 파랑) + 세 평면 핸들.
//
// 일반 메시와 다른 점 둘:
//   1. 뎁스 테스트를 하지 않는다. 오브젝트 안에 파묻혀도 보여야 한다.
//   2. 카메라 거리에 비례해 키운다. 멀어져도 화면에서 같은 크기다.
//      (스케일 = 거리 * kScreenScale - 원근 나눗셈을 상쇄하는 셈)
//
// 평면 핸들(반투명 사각형)이 있는 이유: 카메라를 정면으로 향한 축은 화면에서
// 점으로 보여 집을 수가 없다. 그 축과 수직인 평면 핸들은 그때 가장 잘 보인다.
// 핸들 번호는 축 0~2 에 이어 3~5 이고, 3 + n 은 '법선이 n 축인 평면'이다.
// (3 = YZ 평면, 4 = XZ 평면, 5 = XY 평면)
//
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
    // highlight (0~2 축, 3~5 평면) 는 밝게 그린다 - 끌고 있는 핸들 표시.
    void render(FrameInfo& frame, const vec3& position, int highlight = -1);

    bool isReady() const;

    // 축 방향. 이동 기즈모는 월드 축 그대로다.
    static vec3 axisDirection(int axis);

    // 이 카메라 거리에서 화살표 길이. render 와 hitTestAxis 가 같은 값을 쓴다.
    float arrowLength(const LotCamera& camera, const vec3& position) const;

    // 레이가 어느 핸들을 집었는지. 축 0~2, 평면 3~5, 없으면 -1.
    // 평면 핸들이 축보다 우선이다 - 축과 겹치는 자리에 있어도 작은 쪽을 집는 게 맞다.
    int hitTest(const lot_pick::Ray& ray, const LotCamera& camera,
                const vec3& position) const;

    static bool isPlaneHandle(int handle) { return handle >= 3 && handle <= 5; }
    static int planeNormalAxis(int handle) { return handle - 3; }

    // 레이와 평면 핸들의 평면(position 을 지나고 법선이 axis 인)의 교점.
    // 평면 드래그 중에 마우스가 평면 위 어디를 가리키는지 알아낸다.
    // 레이가 평면과 평행하면 false.
    static bool intersectPlane(const lot_pick::Ray& ray, const vec3& position,
                               int normalAxis, vec3& hitOut);

    // 화면에서 차지하는 크기. 카메라 거리 1 당 월드 단위 길이.
    float screenScale = 0.18f;

    // 축을 집었다고 볼 거리 (화살표 길이에 대한 비율).
    // 머리 반지름(0.08)보다 조금 넉넉하게.
    float pickTolerance = 0.14f;

    // 평면 핸들의 위치와 크기 (화살표 길이에 대한 비율).
    // 원점에서 planeInner 만큼 떨어진 곳부터 planeOuter 까지의 사각형.
    float planeInner = 0.28f;
    float planeOuter = 0.55f;

private:
    void buildArrow(const vec3& origin, const vec3& axis, float length, const vec3& color);
    void buildPlane(const vec3& origin, int normalAxis, float length, const vec3& color,
                    float alpha);

    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<LotDynamicBuffer> buffer_;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    lot_web_device* device_ = nullptr;

    std::vector<Vertex> vertices_;
};
