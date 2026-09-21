#include "grid_render_system.h"
#include "lot_model.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

namespace {

// 격자 크기. 카메라 시작 거리(2.5)와 큐브 간격(1.5)에 맞춘 값이다.
constexpr int kHalfLines = 10;         // 중심에서 양쪽으로 몇 칸
constexpr float kSpacing = 0.5f;
constexpr float kGridZ = 0.0f;         // 바닥 = z = 0 (Z-up). 스케치 Top 평면과 같은 높이.

// 축 선은 눈에 띄게, 나머지는 은은하게. CAD 관례: X 빨강, Y 초록.
const vec3 kAxisXColor{0.8f, 0.25f, 0.25f};   // X 축 - 붉은색
const vec3 kAxisYColor{0.25f, 0.8f, 0.3f};    // Y 축 - 초록색
const vec3 kLineColor{0.28f, 0.28f, 0.28f};

// 선분 목록으로 격자를 만든다. 인덱스 없이 정점 2개가 선 하나.
LotModel::Builder buildGrid() {
    LotModel::Builder builder;
    const float extent = kHalfLines * kSpacing;

    auto pushLine = [&](vec3 a, vec3 b, const vec3& color) {
        for (const vec3& p : {a, b}) {
            builder.vertices.push_back(Vertex::make(p.x, p.y, p.z,
                                                    color.x, color.y, color.z,
                                                    0.0f, -1.0f, 0.0f));  // 조명은 안 쓰지만 레이아웃은 같다
        }
    };

    for (int i = -kHalfLines; i <= kHalfLines; ++i) {
        const float t = i * kSpacing;
        // Y 방향으로 뻗는 선 (x = t). x = 0 이면 Y 축이다.
        pushLine(vec3{t, -extent, kGridZ}, vec3{t, extent, kGridZ},
                 (i == 0) ? kAxisYColor : kLineColor);
        // X 방향으로 뻗는 선 (y = t). y = 0 이면 X 축이다.
        pushLine(vec3{-extent, t, kGridZ}, vec3{extent, t, kGridZ},
                 (i == 0) ? kAxisXColor : kLineColor);
    }
    return builder;
}

}  // namespace

GridRenderSystem::~GridRenderSystem() {
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
}

void GridRenderSystem::create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                              WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat) {
    if (pipeline_) return;
    if (globalLayout == nullptr) {
        LOT_ERR("GridRenderSystem: global uniform layout is required!");
        return;
    }

    model_ = std::make_unique<LotModel>(device, buildGrid());

    // 격자는 오브젝트별 유니폼이 없으므로 레이아웃은 글로벌 하나뿐이다
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Grid Render System Layout");
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &globalLayout;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &layoutDesc);
    if (!pipelineLayout_) {
        LOT_ERR("GridRenderSystem: Failed to create pipeline layout!");
        return;
    }

    PipelineConfig config;
    config.topology = WGPUPrimitiveTopology_LineList;
    config.cullMode = WGPUCullMode_None;  // 선분에는 앞뒤가 없다

    pipeline_ = std::make_unique<lot_web_pipeline>("shaders/unlit.wgsl");
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_, config);
    LOT_LOG("GridRenderSystem: created (" << (2 * kHalfLines + 1) * 2 << " lines)");
}

bool GridRenderSystem::isReady() const {
    return pipeline_ && pipeline_->isReady() && model_ && model_->isReady();
}

void GridRenderSystem::render(FrameInfo& frame) {
    if (!isReady() || frame.pass == nullptr) return;

    pipeline_->bind(frame.pass);
    wgpuRenderPassEncoderSetBindGroup(frame.pass, 0, frame.globalBindGroup, 0, nullptr);
    model_->bind(frame.pass);
    model_->draw(frame.pass);
}
