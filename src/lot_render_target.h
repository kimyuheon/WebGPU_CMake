#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>

class lot_web_device;

// 오프스크린 렌더 타깃 = 색 텍스처 + 뎁스 텍스처.
//
// 스왑체인에 직접 그리면 그 결과를 다시 읽을 수 없다. 여기에 그리면 색과
// 뎁스가 텍스처로 남아서 다음 패스가 샘플링할 수 있다 - 후처리, 외곽선,
// 그림자, GPU 피킹이 전부 이걸 요구한다.
//
// 색 포맷은 스왑체인과 같게 잡는다. 그래야 기존 파이프라인들이 그대로
// 여기에도 그려진다 (파이프라인은 컬러 포맷에 묶여 있다).
class LotRenderTarget {
public:
    LotRenderTarget() = default;
    ~LotRenderTarget();

    LotRenderTarget(const LotRenderTarget&) = delete;
    LotRenderTarget& operator=(const LotRenderTarget&) = delete;

    // 크기가 바뀌었으면 텍스처를 다시 만든다. 매 프레임 불러도 싸다 (같으면 no-op).
    void ensureSize(lot_web_device& device, uint32_t width, uint32_t height,
                    WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);

    // 여기에 그리는 렌더 패스를 연다. 색과 뎁스를 지우고 시작한다.
    WGPURenderPassEncoder beginRenderPass(WGPUCommandEncoder encoder,
                                          const WGPUColor& clearColor);

    WGPUTextureView getColorView() const { return colorView_; }
    WGPUTextureView getDepthView() const { return depthView_; }
    WGPUTextureFormat getColorFormat() const { return colorFormat_; }
    WGPUTextureFormat getDepthFormat() const { return depthFormat_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    bool isReady() const { return colorView_ != nullptr && depthView_ != nullptr; }

private:
    void release();

    WGPUTexture colorTexture_ = nullptr;
    WGPUTextureView colorView_ = nullptr;
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    WGPUTextureFormat colorFormat_ = WGPUTextureFormat_Undefined;
    WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Undefined;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};
