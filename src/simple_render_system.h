#pragma once

#include "lot_web_pipeline.h"
#include "lot_web_buffer.h"
#include "lot_frame_info.h"
#include "lot_material.h"
#include "lot_math.h"
#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>
#include <string>

class lot_web_device;

// 게임 오브젝트(메시)를 그리는 렌더 시스템.
//
// 카메라/조명(@group(0)) 은 LotGlobalUniform 이 바깥에서 관리하고,
// 여기서는 오브젝트별 변환(@group(1)) 과 재질(@group(2)) 을 다룬다.
class SimpleRenderSystem {
public:
    // 한 프레임에 그릴 수 있는 최대 오브젝트 수.
    // uniform 버퍼가 이 개수만큼의 슬롯을 미리 잡는다.
    static constexpr uint32_t kMaxObjects = 1024;

    SimpleRenderSystem(const std::string& shaderPath);
    ~SimpleRenderSystem();

    // 복사 금지
    SimpleRenderSystem(const SimpleRenderSystem&) = delete;
    SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;

    // 초기화.
    // uniform 리소스를 먼저 만들고(바인드 그룹 레이아웃이 여기서 나온다),
    // 그 레이아웃으로 파이프라인을 만든다. 순서가 바뀌면 안 된다.
    //
    // globalLayout 은 LotGlobalUniform::getLayout(). 파이프라인 레이아웃의
    // slot 0 에 들어간다.
    void createUniformBuffer(lot_web_device& device, WGPUBindGroupLayout globalLayout);
    void createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat,
                        WGPUTextureFormat depthFormat);

    // 게임 오브젝트들 렌더링
    void render(FrameInfo& frame);

    // 재질을 만들 때 필요한 @group(2) 레이아웃. createUniformBuffer 뒤에 유효하다.
    WGPUBindGroupLayout getMaterialLayout() const { return materialLayout_; }

    // 상태 확인
    bool isReady() const { return pipeline_->isReady() && uniformCreated_; }
    bool isPipelineReady() const { return pipeline_->isReady(); }
    bool isUniformReady() const { return uniformCreated_; }

private:
    // --- group(1): 오브젝트당 (dynamic offset 으로 슬롯을 옮긴다) ---
    //
    // mat4 가 열 우선이라 WGSL 의 mat4x4<f32> 로 그대로 memcpy 된다.
    // Vulkan 쪽에서는 이걸 push constant 로 보냈지만 WebGPU 에는 push constant 가
    // 없어서, 오브젝트마다 uniform 버퍼의 자기 슬롯에 써넣는다.
    //
    // normalMatrix 는 상단 3x3 만 쓴다. mat3x3 은 WGSL 에서 열마다 16바이트로
    // 패딩되어 C++ 쪽과 어긋나기 쉬우므로 mat4 로 보내는 편이 안전하다.
    struct UniformData {
        mat4 modelMatrix;   // 월드 변환만. 카메라는 group(0) 이 들고 있다.
        mat4 normalMatrix;  // transpose(inverse(mat3(model)))
    };
    static_assert(sizeof(UniformData) == 128, "Uniform layout must match triangle.wgsl");

    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<lot_web_buffer> uniformBuffer_;

    // 텍스처가 없는 오브젝트용 1x1 흰색. 셰이더가 분기 없이 항상 곱한다.
    std::unique_ptr<LotMaterial> defaultMaterial_;

    WGPUQueue queue_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBindGroupLayout materialLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // 오브젝트 하나가 차지하는 uniform 버퍼 간격.
    // sizeof(UniformData) 가 아니라 minUniformBufferOffsetAlignment (보통 256)
    // 배수로 올림해야 dynamic offset 으로 가리킬 수 있다.
    uint32_t uniformStride_ = 0;

    bool uniformCreated_ = false;
    bool overflowWarned_ = false;
};
