#pragma once

#include "lot_dynamic_buffer.h"
#include "lot_frame_info.h"
#include "lot_math.h"
#include "lot_vertex.h"
#include "lot_web_pipeline.h"

#include <webgpu/webgpu.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class lot_web_device;
class LotMaterial;

// 월드 공간의 글자 (치수 값, 문자 도구).
//
// 문자열마다 브라우저 캔버스로 비트맵을 한 번 굽고 (js_renderTextBitmap) 텍스처로
// 캐시한다. 프레임마다 addText 로 '어디에 어떤 방향으로 얼마나 크게' 를 받고, 글자
// 하나가 사각형 하나(정점 6개)다. 같은 텍스처를 쓰는 사각형들을 모아 draw 한 번씩.
// 선 시스템과 같은 immediate 방식이다: clear() -> addText() ... -> render().
//
// Vulkan 쪽은 stb_truetype 글리프를 삼각분할해 메시로 만들지만 결과는 같다 -
// 평면에 놓인, 줌하면 커지는 도면 글자.
class TextRenderSystem {
public:
    TextRenderSystem() = default;
    ~TextRenderSystem();

    TextRenderSystem(const TextRenderSystem&) = delete;
    TextRenderSystem& operator=(const TextRenderSystem&) = delete;

    void create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);

    void clear() { quads_.clear(); }

    // origin 이 글자의 기준점, right/up 은 글자가 놓이는 평면의 단위 축, height 는
    // 글자 높이(월드). align: 0 왼쪽, 1 가운데, 2 오른쪽 (origin 기준). 세로는 항상 가운데.
    void addText(const std::string& text, const vec3& origin, const vec3& right, const vec3& up,
                 float height, const vec3& color, int align = 1);

    // 그 글자가 차지할 폭 (월드). 배치 계산용 - 비트맵을 굽는다 (캐시).
    float measure(const std::string& text, float height);

    void render(FrameInfo& frame);
    bool isReady() const;

private:
    struct Entry {
        std::shared_ptr<LotMaterial> material;
        float aspect = 1.0f;     // 비트맵 폭 / 높이 (여백 포함)
        float quadPerHeight = 1.0f;  // 사각형 높이 / 글자 높이 - 비트맵 여백과 em 대비 대문자 높이 보정
    };
    struct Quad {
        const Entry* entry;
        Vertex v[6];
    };

    const Entry* lookup(const std::string& text);

    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<LotDynamicBuffer> buffer_;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBindGroupLayout materialLayout_ = nullptr;
    lot_web_device* device_ = nullptr;

    std::unordered_map<std::string, Entry> cache_;
    std::vector<Quad> quads_;
    std::vector<Vertex> vertices_;  // render 때 material 별로 정렬해 채운다

    static constexpr int kBitmapHeightPx = 64;  // 비트맵 글자 높이. 줌해도 이 해상도.
};
