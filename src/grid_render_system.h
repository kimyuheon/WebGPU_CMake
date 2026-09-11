#pragma once

#include "lot_web_pipeline.h"
#include "lot_frame_info.h"

#include <webgpu/webgpu.h>
#include <memory>

class lot_web_device;
class LotModel;

// 바닥 격자를 그리는 렌더 시스템.
//
// 렌더 시스템이 둘이 되는 첫 사례라, 구조가 갈라지는 지점을 보여준다:
//   - 파이프라인은 각자 (여기는 선분, 컬링 없음)
//   - 카메라/조명 유니폼(@group(0))은 공유 - FrameInfo 로 받는다
//   - 오브젝트별 유니폼은 없다 (격자는 월드에 고정)
// Vulkan 쪽 grid_render_system 과 같은 자리다.
class GridRenderSystem {
public:
    GridRenderSystem() = default;
    ~GridRenderSystem();

    GridRenderSystem(const GridRenderSystem&) = delete;
    GridRenderSystem& operator=(const GridRenderSystem&) = delete;

    // globalLayout 은 LotGlobalUniform::getLayout().
    void create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);

    void render(FrameInfo& frame);

    bool isReady() const;

private:
    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<LotModel> model_;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
};
