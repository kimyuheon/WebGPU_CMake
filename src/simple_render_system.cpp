#include "simple_render_system.h"
#include <iostream>

// JavaScript 함수 선언 (webgpu_bindings.js에서 구현)
extern "C" {
    extern void js_srs_createUniformBuffer();
    extern void js_srs_updateUniform(float offsetX, float offsetY, float rotation, float scale);
    extern void js_srs_bindUniformGroup();
    extern void js_srs_bindVertexBuffer(int bufferId, int slot);
    extern void js_srs_draw(int vertexCount);
}

// C++ 구현
SimpleRenderSystem::SimpleRenderSystem(const std::string& shaderPath) {
    pipeline_ = std::make_unique<lot_web_pipeline>(shaderPath);
}

SimpleRenderSystem::~SimpleRenderSystem() {
}

void SimpleRenderSystem::createPipeline() {
    pipeline_->createPipeline();
    std::cout << "SimpleRenderSystem: Pipeline creation started" << std::endl;
}

void SimpleRenderSystem::createUniformBuffer() {
    if (!uniformCreated_ && pipeline_->isReady()) {
        js_srs_createUniformBuffer();
        uniformCreated_ = true;
    }
}

bool SimpleRenderSystem::isReady() const {
    return pipeline_->isReady() && uniformCreated_;
}

bool SimpleRenderSystem::isPipelineReady() const {
    return pipeline_->isReady();
}

void SimpleRenderSystem::renderGameObjects(std::vector<LotGameObject>& gameObjects) {
    if (!isReady()) return;

    // 파이프라인 바인딩
    pipeline_->bind();

    for (auto& obj : gameObjects) {
        // Transform 정보로 uniform 업데이트
        const auto& transform = obj.transform2d;

        js_srs_updateUniform(
            transform.translation.x,
            transform.translation.y,
            transform.rotation,
            transform.scale.x
        );

        // Bind group 바인딩
        js_srs_bindUniformGroup();

        // Vertex 버퍼 바인딩
        if (obj.modelBufferId >= 0) {
            js_srs_bindVertexBuffer(obj.modelBufferId, 0);
        }

        // Draw 호출
        if (obj.vertexCount > 0) {
            js_srs_draw(obj.vertexCount);
        }
    }
}
