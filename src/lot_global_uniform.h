#pragma once

#include "lot_camera.h"
#include "lot_lighting.h"
#include "lot_math.h"

#include <webgpu/webgpu.h>
#include <memory>

class lot_web_device;
class lot_web_buffer;

// 프레임당 한 번 갱신되는 유니폼 (카메라 + 조명) = 셰이더의 @group(0).
//
// 예전에는 SimpleRenderSystem 이 들고 있었는데, 렌더 시스템이 둘 이상이 되면
// 전부 같은 카메라와 조명을 봐야 하므로 바깥으로 뺐다. Vulkan 쪽의
// globalDescriptorSet 과 같은 자리다.
//
// 렌더 시스템은 getLayout() 으로 파이프라인 레이아웃을 만들고,
// 프레임마다 getBindGroup() 을 slot 0 에 묶는다.
class LotGlobalUniform {
public:
    // 셰이더의 GlobalUniforms 구조체와 반드시 같은 레이아웃이어야 한다.
    // vec3 는 WGSL 에서 16바이트로 정렬되므로 vec4 로 보내고 w 를 세기로 쓴다.
    struct Data {
        mat4 projection;              // offset 0
        mat4 view;                    // offset 64
        float ambientLightColor[4];   // offset 128, rgb + 세기
        float lightPosition[4];       // offset 144, xyz + 패딩
        float lightColor[4];          // offset 160, rgb + 세기
    };
    static_assert(sizeof(Data) == 176, "Global uniform layout must match the shaders");

    LotGlobalUniform() = default;
    ~LotGlobalUniform();

    LotGlobalUniform(const LotGlobalUniform&) = delete;
    LotGlobalUniform& operator=(const LotGlobalUniform&) = delete;

    void create(lot_web_device& device);

    // 이번 프레임 값을 GPU 버퍼에 쓴다. 렌더 패스 시작 전에 부른다.
    void update(const LotCamera& camera, const SceneLighting& lighting);

    bool isReady() const { return bindGroup_ != nullptr; }
    WGPUBindGroupLayout getLayout() const { return layout_; }
    WGPUBindGroup getBindGroup() const { return bindGroup_; }

private:
    std::unique_ptr<lot_web_buffer> buffer_;
    WGPUQueue queue_ = nullptr;
    WGPUBindGroupLayout layout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
};
