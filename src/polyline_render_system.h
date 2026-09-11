#pragma once

#include "lot_dynamic_buffer.h"
#include "lot_frame_info.h"
#include "lot_math.h"
#include "lot_vertex.h"
#include "lot_web_pipeline.h"

#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>
#include <vector>

class lot_web_device;

// 이어진 점들(폴리라인)을 그리는 렌더 시스템.
//
// 선분 시스템으로도 그릴 수는 있지만 (점마다 선분으로 쪼개서), 점이 N 개면
// 정점이 2(N-1) 개가 된다. 여기서는 LineStrip 토폴로지로 정점 N 개만 쓴다.
//
// 여러 폴리라인을 draw 한 번에 그리는 요령: 인덱스 버퍼에 0xFFFFFFFF 를
// 끼워 넣으면 GPU 가 거기서 스트립을 끊고 새로 시작한다 (primitive restart).
// 파이프라인의 stripIndexFormat 이 이걸 켠다.
//
// Vulkan 쪽 polyline_render_system 과 같은 자리다.
class PolylineRenderSystem {
public:
    // 스트립을 끊는 인덱스 값. uint32 인덱스에서는 이 값으로 정해져 있다.
    static constexpr uint32_t kRestart = 0xFFFFFFFFu;

    PolylineRenderSystem() = default;
    ~PolylineRenderSystem();

    PolylineRenderSystem(const PolylineRenderSystem&) = delete;
    PolylineRenderSystem& operator=(const PolylineRenderSystem&) = delete;

    void create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);

    void clear();

    // closed 면 마지막 점에서 첫 점으로 되돌아와 닫는다 (원, 다각형).
    void addPolyline(const std::vector<vec3>& points, const vec3& color, bool closed = false);

    void render(FrameInfo& frame);

    bool isReady() const;
    size_t getPolylineCount() const { return polylineCount_; }

private:
    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<LotDynamicBuffer> vertexBuffer_;
    std::unique_ptr<LotDynamicBuffer> indexBuffer_;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    lot_web_device* device_ = nullptr;

    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;  // 폴리라인 사이마다 kRestart
    size_t polylineCount_ = 0;
};
