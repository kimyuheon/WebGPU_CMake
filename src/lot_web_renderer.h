#pragma once

#include "lot_web_device.h"
#include "lot_web_swapchain.h"
#include <memory>
#include <cassert>

class LotWebRenderer {
public:
    LotWebRenderer();
    ~LotWebRenderer();

    // 복사 금지
    LotWebRenderer(const LotWebRenderer&) = delete;
    LotWebRenderer& operator=(const LotWebRenderer&) = delete;

    // 초기화 및 상태 확인
    void init();
    bool isReady() const;

    // 프레임 시작/종료
    bool beginFrame();
    void endFrame();

    // 렌더 패스 시작/종료
    void beginRenderPass();
    void endRenderPass();

    // 프레임 상태 확인
    bool isFrameInProgress() const { return isFrameStarted_; }

    // 리사이즈 확인
    bool wasWindowResized() const { return swapchain_->wasResized(); }
    void resetWindowResizedFlag() { swapchain_->resetResizedFlag(); }

    // 컴포넌트 접근
    lot_web_device& getDevice() { return *device_; }
    lot_web_swapchain& getSwapchain() { return *swapchain_; }

    // 동적 크기 (스왑체인에서 가져옴)
    int getWidth() const { return swapchain_->getWidth(); }
    int getHeight() const { return swapchain_->getHeight(); }

private:
    std::unique_ptr<lot_web_device> device_;
    std::unique_ptr<lot_web_swapchain> swapchain_;

    bool isFrameStarted_ = false;
    bool deviceInitialized_ = false;
    bool swapchainCreated_ = false;
};
