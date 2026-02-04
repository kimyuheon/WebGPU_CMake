#pragma once

#include "lot_web_pipeline.h"
#include "lot_web_buffer.h"
#include "lot_game_object.h"
#include <memory>
#include <vector>

class SimpleRenderSystem {
public:
    SimpleRenderSystem(const std::string& shaderPath);
    ~SimpleRenderSystem();

    // 복사 금지
    SimpleRenderSystem(const SimpleRenderSystem&) = delete;
    SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;

    // 초기화
    void createPipeline();
    void createUniformBuffer();

    // 게임 오브젝트들 렌더링
    void renderGameObjects(std::vector<LotGameObject>& gameObjects);

    // 상태 확인
    bool isReady() const;
    bool isPipelineReady() const;
    bool isUniformReady() const { return uniformCreated_; }

private:
    std::unique_ptr<lot_web_pipeline> pipeline_;
    bool uniformCreated_ = false;
};
