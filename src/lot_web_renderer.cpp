#include "lot_web_renderer.h"
#include <iostream>

LotWebRenderer::LotWebRenderer() {
    device_ = std::make_unique<lot_web_device>();
    swapchain_ = std::make_unique<lot_web_swapchain>();
}

LotWebRenderer::~LotWebRenderer() {
}

void LotWebRenderer::init() {
    device_->init();
    std::cout << "Renderer: Device initialization started..." << std::endl;
}

bool LotWebRenderer::isReady() const {
    return deviceInitialized_ && swapchainCreated_;
}

bool LotWebRenderer::beginFrame() {
    // 디바이스 초기화 확인 (한 번만)
    if (!deviceInitialized_ && device_->isInitialized()) {
        swapchain_->createSwapchain();
        deviceInitialized_ = true;
        std::cout << "Renderer: Device initialized, swapchain created." << std::endl;
    }

    // 스왑체인 준비 확인 (한 번만)
    if (deviceInitialized_ && !swapchainCreated_ && swapchain_->isReady()) {
        swapchainCreated_ = true;
        std::cout << "Renderer: Swapchain ready." << std::endl;
        std::cout << "Renderer: Window size " << getWidth() << "x" << getHeight() << std::endl;
    }

    // 아직 준비되지 않음
    if (!swapchainCreated_) {
        return false;
    }

    assert(!isFrameStarted_ && "Cannot call beginFrame while frame is in progress");
    isFrameStarted_ = true;
    return true;
}

void LotWebRenderer::endFrame() {
    assert(isFrameStarted_ && "Cannot call endFrame while frame is not in progress");
    isFrameStarted_ = false;
}

void LotWebRenderer::beginRenderPass() {
    assert(isFrameStarted_ && "Cannot begin render pass if frame not started");
    swapchain_->beginRenderPass();
}

void LotWebRenderer::endRenderPass() {
    assert(isFrameStarted_ && "Cannot end render pass if frame not started");
    swapchain_->endRenderPass();
}
