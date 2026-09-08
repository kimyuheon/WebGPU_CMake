#pragma once

#include "lot_web_pipeline.h"
#include "lot_web_buffer.h"
#include "lot_game_object.h"
#include "lot_camera.h"
#include "lot_lighting.h"
#include "lot_math.h"
#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class lot_web_device;

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
    void createUniformBuffer(lot_web_device& device);
    void createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat,
                        WGPUTextureFormat depthFormat);

    // 게임 오브젝트들 렌더링
    void renderGameObjects(WGPURenderPassEncoder pass,
                           std::vector<LotGameObject>& gameObjects,
                           const LotCamera& camera,
                           const SceneLighting& lighting);

    // 상태 확인
    bool isReady() const { return pipeline_->isReady() && uniformCreated_; }
    bool isPipelineReady() const { return pipeline_->isReady(); }
    bool isUniformReady() const { return uniformCreated_; }

private:
    // --- group(0): 프레임당 한 번 (카메라 + 조명) ---
    //
    // 오브젝트마다 바뀌지 않는 값들이라 따로 뺐다. 예전에는 projection * view 를
    // 오브젝트 유니폼에 미리 곱해 넣었지만, 점 광원은 월드 좌표가 필요해서
    // 셰이더가 model 과 view/projection 을 따로 알아야 한다.
    //
    // vec3 는 WGSL 에서 16바이트로 정렬되므로 vec4 로 보내고 w 를 세기로 쓴다
    // (색상의 w = 세기). 이러면 C++ 과 WGSL 의 오프셋이 어긋날 일이 없다.
    struct GlobalUniformData {
        mat4 projection;              // offset 0
        mat4 view;                    // offset 64
        float ambientLightColor[4];   // offset 128, rgb + 세기
        float lightPosition[4];       // offset 144, xyz + 패딩
        float lightColor[4];          // offset 160, rgb + 세기
    };
    static_assert(sizeof(GlobalUniformData) == 176,
                  "Global uniform layout must match triangle.wgsl");

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
    std::unique_ptr<lot_web_buffer> globalBuffer_;

    WGPUQueue queue_ = nullptr;
    WGPUBindGroupLayout globalBindGroupLayout_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBindGroup globalBindGroup_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // 오브젝트 하나가 차지하는 uniform 버퍼 간격.
    // sizeof(UniformData) 가 아니라 minUniformBufferOffsetAlignment (보통 256)
    // 배수로 올림해야 dynamic offset 으로 가리킬 수 있다.
    uint32_t uniformStride_ = 0;

    bool uniformCreated_ = false;
    bool overflowWarned_ = false;
};
