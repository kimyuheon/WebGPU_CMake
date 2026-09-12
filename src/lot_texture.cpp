#include "lot_texture.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

LotTexture::~LotTexture() {
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (view_) wgpuTextureViewRelease(view_);
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
    }
}

std::unique_ptr<LotTexture> LotTexture::createFromRGBA(lot_web_device& device,
                                                       uint32_t width, uint32_t height,
                                                       const uint8_t* rgba,
                                                       const char* label) {
    if (width == 0 || height == 0 || rgba == nullptr) {
        LOT_ERR("LotTexture: empty image");
        return nullptr;
    }

    auto tex = std::unique_ptr<LotTexture>(new LotTexture());
    tex->width_ = width;
    tex->height_ = height;

    // 1. GPU 텍스처. TextureBinding = 셰이더에서 샘플링, CopyDst = 여기서 쓰기.
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = lotStringView(label);
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = width;
    desc.size.height = height;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;   // 밉맵은 아직 - 멀리서 반짝이면 그때 넣는다
    desc.sampleCount = 1;

    tex->texture_ = wgpuDeviceCreateTexture(device.getDevice(), &desc);
    if (!tex->texture_) {
        LOT_ERR("LotTexture: failed to create texture " << width << "x" << height);
        return nullptr;
    }

    // 2. 픽셀 업로드. bytesPerRow 는 4 바이트 * 너비 - 256 정렬은 writeTexture 에는
    //    필요 없다 (버퍼->텍스처 복사에만 해당).
    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = tex->texture_;
    dst.mipLevel = 0;
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = width * 4;
    layout.rowsPerImage = height;

    WGPUExtent3D size{width, height, 1};
    wgpuQueueWriteTexture(device.getQueue(), &dst, rgba,
                          static_cast<size_t>(width) * height * 4, &layout, &size);

    // 3. 뷰
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.label = lotStringView(label);
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    tex->view_ = wgpuTextureCreateView(tex->texture_, &viewDesc);

    // 4. 샘플러. 선형 필터 + 반복. 체커보드 같은 픽셀 아트는 Nearest 가 나을 수
    //    있지만, 일반 텍스처 기본값으로는 Linear 가 맞다.
    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.label = lotStringView("Sampler");
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.maxAnisotropy = 1;
    tex->sampler_ = wgpuDeviceCreateSampler(device.getDevice(), &samplerDesc);

    if (!tex->isReady()) {
        LOT_ERR("LotTexture: failed to create view/sampler");
        return nullptr;
    }

    LOT_LOG("LotTexture: " << label << " " << width << "x" << height);
    return tex;
}

std::unique_ptr<LotTexture> LotTexture::createSolid(lot_web_device& device,
                                                    uint8_t r, uint8_t g, uint8_t b,
                                                    uint8_t a) {
    const uint8_t pixel[4] = {r, g, b, a};
    return createFromRGBA(device, 1, 1, pixel, "Solid 1x1");
}

std::unique_ptr<LotTexture> LotTexture::createChecker(lot_web_device& device,
                                                      uint32_t size, uint32_t cell,
                                                      const uint8_t colorA[3],
                                                      const uint8_t colorB[3]) {
    if (cell == 0) cell = 1;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const bool odd = ((x / cell) + (y / cell)) % 2 == 1;
            const uint8_t* c = odd ? colorB : colorA;
            uint8_t* p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
        }
    }
    return createFromRGBA(device, size, size, pixels.data(), "Checker");
}
