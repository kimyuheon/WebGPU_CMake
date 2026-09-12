#pragma once

#include "lot_web_pipeline.h"

#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>

class lot_web_device;
class lot_web_buffer;
class LotRenderTarget;

// 후처리 - 오프스크린 타깃의 색/뎁스를 읽어 화면에 그린다.
//
// 렌더 타깃이 다시 만들어지면(리사이즈) 텍스처 뷰가 바뀌므로 바인드 그룹도
// 다시 만들어야 한다. render() 가 뷰 포인터를 비교해 알아서 한다.
class PostProcessSystem {
public:
    enum class Mode : uint32_t {
        Passthrough = 0,
        Outline = 1,
    };

    PostProcessSystem() = default;
    ~PostProcessSystem();

    PostProcessSystem(const PostProcessSystem&) = delete;
    PostProcessSystem& operator=(const PostProcessSystem&) = delete;

    // 화면(스왑체인)에 그리므로 colorFormat 은 스왑체인 포맷. 뎁스는 없다.
    void create(lot_web_device& device, WGPUTextureFormat colorFormat);

    // 스왑체인 패스 안에서 부른다. target 의 색/뎁스를 읽어 pass 에 그린다.
    void render(WGPURenderPassEncoder pass, const LotRenderTarget& target);

    bool isReady() const;

    Mode mode = Mode::Passthrough;
    uint32_t outlineWidth = 1;

private:
    struct Uniforms {
        uint32_t mode;
        uint32_t outlineWidth;
        uint32_t pad0;
        uint32_t pad1;
    };
    static_assert(sizeof(Uniforms) == 16, "PostUniforms layout must match post.wgsl");

    void rebuildBindGroup(const LotRenderTarget& target);

    lot_web_device* device_ = nullptr;
    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<lot_web_buffer> uniformBuffer_;
    WGPUBindGroupLayout layout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;

    // 바인드 그룹이 가리키는 뷰. 바뀌면 다시 만든다.
    WGPUTextureView boundColor_ = nullptr;
    WGPUTextureView boundDepth_ = nullptr;
};
