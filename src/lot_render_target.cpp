#include "lot_render_target.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

namespace {

WGPUTexture createAttachment(WGPUDevice device, const char* label, uint32_t width,
                             uint32_t height, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = lotStringView(label);
    // RenderAttachment = 여기에 그린다, TextureBinding = 다음 패스가 읽는다.
    // 둘 다 켜는 것이 오프스크린 타깃의 핵심이다. 스왑체인 텍스처는 두 번째가 없다.
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depthOrArrayLayers = 1;
    desc.format = format;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    return wgpuDeviceCreateTexture(device, &desc);
}

WGPUTextureView createView(WGPUTexture texture, const char* label, WGPUTextureFormat format) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.label = lotStringView(label);
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.mipLevelCount = 1;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    return wgpuTextureCreateView(texture, &desc);
}

}  // namespace

LotRenderTarget::~LotRenderTarget() {
    release();
}

void LotRenderTarget::release() {
    if (colorView_) { wgpuTextureViewRelease(colorView_); colorView_ = nullptr; }
    if (depthView_) { wgpuTextureViewRelease(depthView_); depthView_ = nullptr; }
    if (colorTexture_) {
        wgpuTextureDestroy(colorTexture_);
        wgpuTextureRelease(colorTexture_);
        colorTexture_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
}

void LotRenderTarget::ensureSize(lot_web_device& device, uint32_t width, uint32_t height,
                                 WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat) {
    if (width == 0 || height == 0) return;
    if (isReady() && width == width_ && height == height_
        && colorFormat == colorFormat_ && depthFormat == depthFormat_) {
        return;
    }

    release();
    width_ = width;
    height_ = height;
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;

    colorTexture_ = createAttachment(device.getDevice(), "Offscreen Color", width, height,
                                     colorFormat);
    depthTexture_ = createAttachment(device.getDevice(), "Offscreen Depth", width, height,
                                     depthFormat);
    if (!colorTexture_ || !depthTexture_) {
        LOT_ERR("LotRenderTarget: failed to create textures " << width << "x" << height);
        release();
        return;
    }
    colorView_ = createView(colorTexture_, "Offscreen Color View", colorFormat);
    depthView_ = createView(depthTexture_, "Offscreen Depth View", depthFormat);

    LOT_LOG("LotRenderTarget: " << width << "x" << height);
}

WGPURenderPassEncoder LotRenderTarget::beginRenderPass(WGPUCommandEncoder encoder,
                                                       const WGPUColor& clearColor) {
    if (!isReady() || encoder == nullptr) return nullptr;

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = colorView_;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = clearColor;

    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depth.view = depthView_;
    depth.depthLoadOp = WGPULoadOp_Clear;
    depth.depthStoreOp = WGPUStoreOp_Store;  // 다음 패스가 읽으므로 버리면 안 된다
    depth.depthClearValue = 1.0f;

    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.label = lotStringView("Offscreen Pass");
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    desc.depthStencilAttachment = &depth;

    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}
