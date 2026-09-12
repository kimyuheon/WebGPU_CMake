#pragma once

#include "lot_web_device.h"
#include "lot_web_swapchain.h"
#include <webgpu/webgpu.h>
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
    bool isReady() const { return deviceInitialized_ && swapchainCreated_; }

    // 프레임 시작/종료
    bool beginFrame();
    void endFrame();

    // 렌더 패스 시작/종료 (스왑체인에 그리는 패스).
    // withDepth = false 면 뎁스 어태치먼트 없이 연다 - 후처리처럼 화면을 한 번
    // 덮어쓰는 패스용. 그 패스의 파이프라인도 뎁스 없이 만들어야 한다.
    void beginRenderPass(bool withDepth = true);
    void endRenderPass();

    // 화면에 이미 그려진 것 위에 덧그리는 패스. 색은 지우지 않고(Load) 이어 그리고,
    // 뎁스는 바깥에서 준 뷰(보통 오프스크린 장면의 뎁스)를 그대로 쓴다 - 그래서
    // 격자/보조선이 후처리 뒤에 그려지면서도 메시에 제대로 가려진다.
    void beginOverlayPass(WGPUTextureView depthView);

    // 이번 프레임의 커맨드 인코더. 오프스크린 패스를 열 때 쓴다.
    WGPUCommandEncoder getCurrentEncoder() const {
        assert(isFrameStarted_ && "Cannot get encoder when frame not in progress");
        return currentEncoder_;
    }

    // 현재 프레임의 렌더 패스 (렌더 시스템이 여기에 명령을 기록한다)
    WGPURenderPassEncoder getCurrentRenderPass() const {
        assert(isFrameStarted_ && "Cannot get render pass when frame not in progress");
        return currentPass_;
    }

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
    float getAspectRatio() const {
        return static_cast<float>(swapchain_->getWidth())
             / static_cast<float>(swapchain_->getHeight());
    }

private:
    std::unique_ptr<lot_web_device> device_;
    std::unique_ptr<lot_web_swapchain> swapchain_;

    WGPUCommandEncoder currentEncoder_ = nullptr;
    WGPURenderPassEncoder currentPass_ = nullptr;
    WGPUTextureView currentView_ = nullptr;

    bool isFrameStarted_ = false;
    bool deviceInitialized_ = false;
    bool swapchainCreated_ = false;
};
