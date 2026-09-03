#pragma once

#include "lot_web_pipeline.h"
#include "lot_web_buffer.h"
#include "lot_game_object.h"
#include <webgpu/webgpu.h>
#include <memory>
#include <string>
#include <vector>

class lot_web_device;

class SimpleRenderSystem {
public:
    SimpleRenderSystem(const std::string& shaderPath);
    ~SimpleRenderSystem();

    // 복사 금지
    SimpleRenderSystem(const SimpleRenderSystem&) = delete;
    SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;

    // 초기화
    void createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat);
    void createUniformBuffer(lot_web_device& device);

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
    WGPUBindGroup bindGroup_ = nullptr;
    bool uniformCreated_ = false;
};
