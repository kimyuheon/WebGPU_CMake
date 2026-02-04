#pragma once

#include <emscripten/emscripten.h>

class lot_web_swapchain {
public:
    lot_web_swapchain();
    ~lot_web_swapchain();

    // 스왑체인 생성 (JavaScript에서 처리)
    void createSwapchain();

    // 리사이즈 처리
    void resize(int width, int height);

    // 현재 프레임의 렌더 패스 시작
    void beginRenderPass();

    // 현재 프레임의 렌더 패스 종료 및 제출
    void endRenderPass();

    // 스왑체인이 준비되었는지 확인
    bool isReady() const;

    // 리사이즈가 발생했는지 확인
    bool wasResized() const { return wasResized_; }
    void resetResizedFlag() { wasResized_ = false; }

    // 크기 정보
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    bool wasResized_ = false;
};
