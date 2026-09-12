#include "line_render_system.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

LineRenderSystem::~LineRenderSystem() {
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
}

void LineRenderSystem::create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                              WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat) {
    if (pipeline_) return;
    if (globalLayout == nullptr) {
        LOT_ERR("LineRenderSystem: global uniform layout is required!");
        return;
    }

    device_ = &device;
    buffer_ = std::make_unique<LotDynamicBuffer>(BufferType::VERTEX);

    // 선은 월드 좌표 그대로라 오브젝트별 유니폼이 없다
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Line Render System Layout");
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &globalLayout;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &layoutDesc);
    if (!pipelineLayout_) {
        LOT_ERR("LineRenderSystem: Failed to create pipeline layout!");
        return;
    }

    PipelineConfig config;
    config.topology = WGPUPrimitiveTopology_LineList;
    config.cullMode = WGPUCullMode_None;

    pipeline_ = std::make_unique<lot_web_pipeline>("shaders/unlit.wgsl");
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_, config);
    LOT_LOG("LineRenderSystem: created");
}

bool LineRenderSystem::isReady() const {
    return pipeline_ && pipeline_->isReady() && buffer_ != nullptr;
}

void LineRenderSystem::addLine(const vec3& a, const vec3& b, const vec3& color) {
    for (const vec3& p : {a, b}) {
        vertices_.push_back(Vertex{{p.x, p.y, p.z},
                                   {color.x, color.y, color.z},
                                   {0.0f, -1.0f, 0.0f}});  // 조명은 안 쓰지만 레이아웃은 같다
    }
}

void LineRenderSystem::addCross(const vec3& c, float h, const vec3& color) {
    addLine(vec3{c.x - h, c.y, c.z}, vec3{c.x + h, c.y, c.z}, color);
    addLine(vec3{c.x, c.y - h, c.z}, vec3{c.x, c.y + h, c.z}, color);
    addLine(vec3{c.x, c.y, c.z - h}, vec3{c.x, c.y, c.z + h}, color);
}

void LineRenderSystem::addBox(const vec3& lo, const vec3& hi, const vec3& color) {
    addTransformedBox(lo, hi, mat4::identity(), color);
}

void LineRenderSystem::addTransformedBox(const vec3& lo, const vec3& hi,
                                         const mat4& transform, const vec3& color) {
    // 여덟 꼭짓점을 비트로 고른다: bit0 = x, bit1 = y, bit2 = z (0 이면 lo, 1 이면 hi)
    auto corner = [&](int bits) {
        return transformPoint(transform,
                              vec3{(bits & 1) ? hi.x : lo.x,
                                   (bits & 2) ? hi.y : lo.y,
                                   (bits & 4) ? hi.z : lo.z});
    };
    // 모서리 12 개 = 비트 하나만 다른 꼭짓점 쌍
    for (int i = 0; i < 8; ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            const int j = i | (1 << axis);
            if (j != i) addLine(corner(i), corner(j), color);
        }
    }
}

void LineRenderSystem::render(FrameInfo& frame) {
    if (!isReady() || frame.pass == nullptr || device_ == nullptr) return;

    // 그릴 게 없어도 upload 는 한다 - size 를 0 으로 맞춰야 이전 프레임 것이 남지 않는다
    buffer_->upload(*device_, vertices_.data(), vertices_.size() * sizeof(Vertex));
    if (vertices_.empty()) return;

    pipeline_->bind(frame.pass);
    wgpuRenderPassEncoderSetBindGroup(frame.pass, 0, frame.globalBindGroup, 0, nullptr);
    buffer_->bind(frame.pass, 0);
    wgpuRenderPassEncoderDraw(frame.pass, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}
