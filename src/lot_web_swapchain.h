#pragma once

#include <webgpu/webgpu.h>

class lot_web_device;

// WebGPU 에는 더 이상 스왑체인 객체가 없다 (WGPUSwapChain 은 제거됨).
// 캔버스는 WGPUSurface 로 표현하고 wgpuSurfaceConfigure 로 재설정한다.
// 클래스 이름은 Vulkan 쪽 구조와 맞추기 위해 그대로 둔다.
class lot_web_swapchain {
public:
    lot_web_swapchain();
    ~lot_web_swapchain();

    // 복사 금지
    lot_web_swapchain(const lot_web_swapchain&) = delete;
    lot_web_swapchain& operator=(const lot_web_swapchain&) = delete;

    // 서피스 생성 및 설정 (디바이스 준비 후 한 번)
    void createSwapchain(lot_web_device& device);

    // 리사이즈 처리
    void resize(int width, int height);

    // 이번 프레임에 그릴 텍스처 뷰를 획득 (실패 시 nullptr)
    WGPUTextureView acquireNextImage();

    // 획득한 텍스처 뷰 해제
    void releaseCurrentImage();

    // 뎁스 버퍼 (스왑체인과 같은 크기로 유지된다)
    WGPUTextureView getDepthView() const { return depthView_; }
    WGPUTextureFormat getDepthFormat() const { return kDepthFormat; }

    bool isReady() const { return configured_; }

    bool wasResized() const { return wasResized_; }
    void resetResizedFlag() { wasResized_ = false; }

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    WGPUTextureFormat getFormat() const { return format_; }

private:
    static constexpr WGPUTextureFormat kDepthFormat = WGPUTextureFormat_Depth24Plus;

    void configure();
    void createDepthResources();
    void releaseDepthResources();

    WGPUSurface surface_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUTextureFormat format_ = WGPUTextureFormat_Undefined;

    WGPUTexture currentTexture_ = nullptr;
    WGPUTextureView currentView_ = nullptr;

    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    bool configured_ = false;
    bool wasResized_ = false;
};
