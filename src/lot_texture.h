#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>
#include <vector>

class lot_web_device;

// 2D 텍스처 + 샘플러.
//
// 입력은 항상 RGBA8 바이트 배열이다. 어디서 왔는지는 안 가린다 - 절차적으로
// 만들었든, JS 가 <img> 를 캔버스에 그려 뽑아줬든, 결국 여기로 모인다.
// (브라우저에서는 PNG 디코딩을 JS 에 맡기는 게 stb_image 를 넣는 것보다
// 가볍고 빠르다. 그 경로는 아직 없다.)
class LotTexture {
public:
    LotTexture() = default;
    ~LotTexture();

    LotTexture(const LotTexture&) = delete;
    LotTexture& operator=(const LotTexture&) = delete;

    // rgba 는 width * height * 4 바이트, 왼쪽 위부터 행 순서.
    static std::unique_ptr<LotTexture> createFromRGBA(lot_web_device& device,
                                                      uint32_t width, uint32_t height,
                                                      const uint8_t* rgba,
                                                      const char* label = "Texture");

    // 단색 1x1. 텍스처가 없는 오브젝트의 기본값 - 셰이더 분기 없이 곱해도 색이 안 변한다.
    static std::unique_ptr<LotTexture> createSolid(lot_web_device& device,
                                                   uint8_t r, uint8_t g, uint8_t b,
                                                   uint8_t a = 255);

    // 체커보드. 텍스처 파이프라인이 도는지, UV 가 맞는지 눈으로 보는 용도.
    static std::unique_ptr<LotTexture> createChecker(lot_web_device& device,
                                                     uint32_t size, uint32_t cell,
                                                     const uint8_t colorA[3],
                                                     const uint8_t colorB[3]);

    WGPUTextureView getView() const { return view_; }
    WGPUSampler getSampler() const { return sampler_; }
    bool isReady() const { return view_ != nullptr && sampler_ != nullptr; }

    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }

private:
    WGPUTexture texture_ = nullptr;
    WGPUTextureView view_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};
