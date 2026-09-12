#pragma once

#include "lot_dynamic_buffer.h"
#include "lot_frame_info.h"
#include "lot_math.h"
#include "lot_vertex.h"
#include "lot_web_pipeline.h"

#include <webgpu/webgpu.h>
#include <memory>
#include <vector>

class lot_web_device;

// 선분을 그리는 렌더 시스템.
//
// 격자와 다른 점은 내용이 '데이터'라는 것이다. 격자는 만들 때 한 번 굽고
// 끝이지만, 선분은 앱이 프레임마다 넣고 빼고 한다 (CAD 의 선 도구, 보조선,
// 광원 위치 표시 등). 그래서 정점 버퍼가 동적이다.
//
// 쓰는 법: 프레임 시작에 clear(), 그릴 만큼 addLine(), render() 가 올린다.
// Vulkan 쪽 line_render_system 과 같은 자리다.
class LineRenderSystem {
public:
    LineRenderSystem() = default;
    ~LineRenderSystem();

    LineRenderSystem(const LineRenderSystem&) = delete;
    LineRenderSystem& operator=(const LineRenderSystem&) = delete;

    void create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat);

    // 이번 프레임에 그릴 선분들
    void clear() { vertices_.clear(); }
    void addLine(const vec3& a, const vec3& b, const vec3& color);

    // 자주 쓰는 모양. 점 하나를 표시할 때 (광원, 스냅 포인트 등).
    void addCross(const vec3& center, float halfSize, const vec3& color);

    // 축에 정렬된 상자 테두리 (경계 상자 표시용).
    void addBox(const vec3& min, const vec3& max, const vec3& color);

    // 상자를 행렬로 돌린 뒤 테두리. 회전한 오브젝트의 경계 상자(OBB)를
    // 그릴 때 - 여덟 꼭짓점을 변환하므로 상자도 같이 기운다.
    void addTransformedBox(const vec3& min, const vec3& max, const mat4& transform,
                           const vec3& color);

    void render(FrameInfo& frame);

    bool isReady() const;
    size_t getLineCount() const { return vertices_.size() / 2; }

private:
    std::unique_ptr<lot_web_pipeline> pipeline_;
    std::unique_ptr<LotDynamicBuffer> buffer_;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    lot_web_device* device_ = nullptr;

    // CPU 쪽 정점. 선 하나가 정점 둘. render() 때 통째로 올린다.
    std::vector<Vertex> vertices_;
};
