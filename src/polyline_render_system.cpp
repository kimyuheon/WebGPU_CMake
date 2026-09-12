#include "polyline_render_system.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

PolylineRenderSystem::~PolylineRenderSystem() {
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
}

void PolylineRenderSystem::create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                                  WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat) {
    if (pipeline_) return;
    if (globalLayout == nullptr) {
        LOT_ERR("PolylineRenderSystem: global uniform layout is required!");
        return;
    }

    device_ = &device;
    vertexBuffer_ = std::make_unique<LotDynamicBuffer>(BufferType::VERTEX);
    indexBuffer_ = std::make_unique<LotDynamicBuffer>(BufferType::INDEX);

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Polyline Render System Layout");
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &globalLayout;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &layoutDesc);
    if (!pipelineLayout_) {
        LOT_ERR("PolylineRenderSystem: Failed to create pipeline layout!");
        return;
    }

    PipelineConfig config;
    config.topology = WGPUPrimitiveTopology_LineStrip;  // stripIndexFormat 은 파이프라인이 알아서 켠다
    config.cullMode = WGPUCullMode_None;

    pipeline_ = std::make_unique<lot_web_pipeline>("shaders/unlit.wgsl");
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_, config);
    LOT_LOG("PolylineRenderSystem: created");
}

bool PolylineRenderSystem::isReady() const {
    return pipeline_ && pipeline_->isReady() && vertexBuffer_ && indexBuffer_;
}

void PolylineRenderSystem::clear() {
    vertices_.clear();
    indices_.clear();
    polylineCount_ = 0;
}

void PolylineRenderSystem::addPolyline(const std::vector<vec3>& points, const vec3& color,
                                       bool closed) {
    if (points.size() < 2) return;

    // 앞에 폴리라인이 있었으면 끊는다. 첫 번째 앞에는 넣지 않는다 -
    // 넣어도 동작은 하지만 인덱스만 낭비다.
    if (!indices_.empty()) {
        indices_.push_back(kRestart);
    }

    const auto base = static_cast<uint32_t>(vertices_.size());
    for (const vec3& p : points) {
        vertices_.push_back(Vertex::make(p.x, p.y, p.z,
                                         color.x, color.y, color.z,
                                         0.0f, -1.0f, 0.0f));
    }
    for (uint32_t i = 0; i < points.size(); ++i) {
        indices_.push_back(base + i);
    }
    if (closed) {
        indices_.push_back(base);  // 첫 점으로 되돌아온다 - 정점을 복제할 필요가 없다
    }
    ++polylineCount_;
}

void PolylineRenderSystem::render(FrameInfo& frame) {
    if (!isReady() || frame.pass == nullptr || device_ == nullptr) return;

    vertexBuffer_->upload(*device_, vertices_.data(), vertices_.size() * sizeof(Vertex));
    indexBuffer_->upload(*device_, indices_.data(), indices_.size() * sizeof(uint32_t));
    if (indices_.empty()) return;

    pipeline_->bind(frame.pass);
    wgpuRenderPassEncoderSetBindGroup(frame.pass, 0, frame.globalBindGroup, 0, nullptr);
    vertexBuffer_->bind(frame.pass, 0);
    indexBuffer_->bind(frame.pass);
    wgpuRenderPassEncoderDrawIndexed(frame.pass, static_cast<uint32_t>(indices_.size()),
                                     1, 0, 0, 0);
}
