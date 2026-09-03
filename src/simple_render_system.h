#pragma once

#include "lot_web_pipeline.h"
#include "lot_web_buffer.h"
#include "lot_game_object.h"
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
    void createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat);

    // 게임 오브젝트들 렌더링
    void renderGameObjects(WGPURenderPassEncoder pass, std::vector<LotGameObject>& gameObjects);

    // 상태 확인
    bool isReady() const { return pipeline_->isReady() && uniformCreated_; }
    bool isPipelineReady() const { return pipeline_->isReady(); }
    bool isUniformReady() const { return uniformCreated_; }

private:
    // 셰이더의 Uniforms 구조체와 반드시 같은 레이아웃이어야 한다.
    //   offset: vec2<f32>  (offset 0)
    //   rotation: f32      (offset 8)
    //   scale: f32         (offset 12)
    struct UniformData {
        float offsetX;
        float offsetY;
        float rotation;
        float scale;
    };
    static_assert(sizeof(UniformData) == 16, "Uniform layout must match triangle.wgsl");

    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<lot_web_buffer> uniformBuffer_;

    WGPUQueue queue_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // 오브젝트 하나가 차지하는 uniform 버퍼 간격.
    // sizeof(UniformData) 가 아니라 minUniformBufferOffsetAlignment (보통 256)
    // 배수로 올림해야 dynamic offset 으로 가리킬 수 있다.
    uint32_t uniformStride_ = 0;

    bool uniformCreated_ = false;
    bool overflowWarned_ = false;
};
