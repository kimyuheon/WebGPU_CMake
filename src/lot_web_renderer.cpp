#include "lot_web_renderer.h"
#include "lot_web_common.h"
#include "lot_log.h"

LotWebRenderer::LotWebRenderer() {
    device_ = std::make_unique<lot_web_device>();
    swapchain_ = std::make_unique<lot_web_swapchain>();
}

LotWebRenderer::~LotWebRenderer() {
    if (currentPass_) wgpuRenderPassEncoderRelease(currentPass_);
    if (currentEncoder_) wgpuCommandEncoderRelease(currentEncoder_);
}

void LotWebRenderer::init() {
    device_->init();
    LOT_LOG("Renderer: Device initialization started...");
}

bool LotWebRenderer::beginFrame() {
    // 디바이스 초기화 확인 (한 번만)
    if (!deviceInitialized_ && device_->isInitialized()) {
        swapchain_->createSwapchain(*device_);
        deviceInitialized_ = true;
        LOT_LOG("Renderer: Device initialized, swapchain created.");
    }

    // 스왑체인 준비 확인 (한 번만)
    if (deviceInitialized_ && !swapchainCreated_ && swapchain_->isReady()) {
        swapchainCreated_ = true;
        LOT_LOG("Renderer: Swapchain ready.");
        LOT_LOG("Renderer: Window size " << getWidth() << "x" << getHeight());
    }

    // 아직 준비되지 않음
    if (!swapchainCreated_) {
        return false;
    }

    assert(!isFrameStarted_ && "Cannot call beginFrame while frame is in progress");

    // 이번 프레임에 그릴 텍스처 획득
    currentView_ = swapchain_->acquireNextImage();
    if (!currentView_) {
        return false;  // 탭이 백그라운드인 경우 등 - 이번 프레임은 건너뛴다
    }

    WGPUCommandEncoderDescriptor encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encoderDesc.label = lotStringView("Frame Encoder");
    currentEncoder_ = wgpuDeviceCreateCommandEncoder(device_->getDevice(), &encoderDesc);

    isFrameStarted_ = true;
    return true;
}

void LotWebRenderer::beginRenderPass(bool withDepth) {
    assert(isFrameStarted_ && "Cannot begin render pass if frame not started");

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = currentView_;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.1, 0.1, 0.1, 1.0};

    // 뎁스 어태치먼트 - 매 프레임 가장 먼 값(1.0)으로 지운다.
    WGPURenderPassDepthStencilAttachment depthAttachment =
        WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depthAttachment.view = swapchain_->getDepthView();
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.label = lotStringView("Main Render Pass");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    if (withDepth && depthAttachment.view != nullptr) {
        passDesc.depthStencilAttachment = &depthAttachment;
    }

    currentPass_ = wgpuCommandEncoderBeginRenderPass(currentEncoder_, &passDesc);
}

void LotWebRenderer::beginOverlayPass(WGPUTextureView depthView) {
    assert(isFrameStarted_ && "Cannot begin overlay pass if frame not started");

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = currentView_;
    colorAttachment.loadOp = WGPULoadOp_Load;   // 후처리 결과 위에 덧그린다
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDepthStencilAttachment depthAttachment =
        WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Load;   // 장면의 뎁스를 그대로
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    // Load 라서 쓰이지 않는 값이지만 반드시 넣어야 한다. INIT 매크로의 기본값이
    // NaN 이고, JS 바인딩이 그걸 "non-finite" 로 거부해 패스 생성이 통째로 실패한다.
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.label = lotStringView("Overlay Pass");
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    if (depthView != nullptr) {
        passDesc.depthStencilAttachment = &depthAttachment;
    }

    currentPass_ = wgpuCommandEncoderBeginRenderPass(currentEncoder_, &passDesc);
}

void LotWebRenderer::endRenderPass() {
    assert(isFrameStarted_ && "Cannot end render pass if frame not started");
    if (!currentPass_) return;

    wgpuRenderPassEncoderEnd(currentPass_);
    wgpuRenderPassEncoderRelease(currentPass_);
    currentPass_ = nullptr;
}

void LotWebRenderer::endFrame() {
    assert(isFrameStarted_ && "Cannot call endFrame while frame is not in progress");

    WGPUCommandBufferDescriptor cmdDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(currentEncoder_, &cmdDesc);

    wgpuQueueSubmit(device_->getQueue(), 1, &commands);

    wgpuCommandBufferRelease(commands);
    wgpuCommandEncoderRelease(currentEncoder_);
    currentEncoder_ = nullptr;

    // 주의: 웹에서는 wgpuSurfacePresent 를 부르면 abort 한다.
    // 브라우저가 rAF 시점에 알아서 표시하므로 present 호출이 없다.
    swapchain_->releaseCurrentImage();
    currentView_ = nullptr;

    isFrameStarted_ = false;
}
