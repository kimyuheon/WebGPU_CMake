#include "gizmo_render_system.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

#include <cmath>

namespace {

// 화살표 비율 (길이 1 기준)
constexpr float kShaftRadius = 0.025f;
constexpr float kHeadLength = 0.25f;
constexpr float kHeadRadius = 0.08f;
constexpr int kSegments = 12;

const vec3 kColorX{0.9f, 0.2f, 0.2f};
const vec3 kColorY{0.2f, 0.85f, 0.2f};
const vec3 kColorZ{0.25f, 0.45f, 0.95f};

// axis 에 수직인 두 벡터. 축 방향 원을 그릴 때 쓴다.
void perpendicular(const vec3& axis, vec3& out1, vec3& out2) {
    // axis 와 가장 덜 평행한 기준 벡터를 고른다 - cross 가 0 이 되지 않도록
    const vec3 ref = (std::fabs(axis.x) < 0.9f) ? vec3{1.0f, 0.0f, 0.0f}
                                                : vec3{0.0f, 1.0f, 0.0f};
    out1 = normalize(cross(axis, ref));
    out2 = cross(axis, out1);
}

}  // namespace

GizmoRenderSystem::~GizmoRenderSystem() {
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
}

void GizmoRenderSystem::create(lot_web_device& device, WGPUBindGroupLayout globalLayout,
                               WGPUTextureFormat colorFormat, WGPUTextureFormat depthFormat) {
    if (pipeline_) return;
    if (globalLayout == nullptr) {
        LOT_ERR("GizmoRenderSystem: global uniform layout is required!");
        return;
    }

    device_ = &device;
    buffer_ = std::make_unique<LotDynamicBuffer>(BufferType::VERTEX);

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Gizmo Render System Layout");
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &globalLayout;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &layoutDesc);
    if (!pipelineLayout_) {
        LOT_ERR("GizmoRenderSystem: Failed to create pipeline layout!");
        return;
    }

    PipelineConfig config;
    config.topology = WGPUPrimitiveTopology_TriangleList;
    config.cullMode = WGPUCullMode_None;  // 원뿔 안쪽이 보이는 각도가 있다
    config.depthTest = false;             // 항상 위에

    pipeline_ = std::make_unique<lot_web_pipeline>("shaders/unlit.wgsl");
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_, config);
    LOT_LOG("GizmoRenderSystem: created");
}

bool GizmoRenderSystem::isReady() const {
    return pipeline_ && pipeline_->isReady() && buffer_ != nullptr;
}

void GizmoRenderSystem::buildArrow(const vec3& origin, const vec3& axis, float length,
                                   const vec3& color) {
    vec3 p1, p2;
    perpendicular(axis, p1, p2);

    auto push = [&](const vec3& p) {
        vertices_.push_back(Vertex{{p.x, p.y, p.z},
                                   {color.x, color.y, color.z},
                                   {0.0f, -1.0f, 0.0f}});
    };
    auto ring = [&](float along, float radius, int i) {
        const float a = 6.2831853f * i / kSegments;
        return origin + axis * along + (p1 * std::cos(a) + p2 * std::sin(a)) * radius;
    };

    const float shaftEnd = length * (1.0f - kHeadLength);
    const float shaftR = length * kShaftRadius;
    const float headR = length * kHeadRadius;
    const vec3 tip = origin + axis * length;

    for (int i = 0; i < kSegments; ++i) {
        const int j = (i + 1) % kSegments;

        // 몸통 - 옆면 사각형을 삼각형 둘로
        const vec3 a0 = ring(0.0f, shaftR, i), a1 = ring(0.0f, shaftR, j);
        const vec3 b0 = ring(shaftEnd, shaftR, i), b1 = ring(shaftEnd, shaftR, j);
        push(a0); push(b0); push(b1);
        push(a0); push(b1); push(a1);

        // 머리 - 원뿔 옆면
        const vec3 c0 = ring(shaftEnd, headR, i), c1 = ring(shaftEnd, headR, j);
        push(c0); push(tip); push(c1);

        // 머리 - 원뿔 밑면 (뒤에서 봤을 때 뚫려 보이지 않도록)
        const vec3 center = origin + axis * shaftEnd;
        push(c1); push(center); push(c0);
    }
}

void GizmoRenderSystem::render(FrameInfo& frame, const vec3& position) {
    if (!isReady() || frame.pass == nullptr || device_ == nullptr) return;

    // 카메라 거리에 비례해 키운다. 원근 나눗셈이 1/거리로 줄이는 것을 상쇄한다.
    const vec3 toCamera = frame.camera.getPosition() - position;
    const float distance = std::sqrt(dot(toCamera, toCamera));
    const float length = std::fmax(distance * screenScale, 0.05f);

    vertices_.clear();
    buildArrow(position, vec3{1.0f, 0.0f, 0.0f}, length, kColorX);
    buildArrow(position, vec3{0.0f, 1.0f, 0.0f}, length, kColorY);  // +Y 는 아래쪽이다
    buildArrow(position, vec3{0.0f, 0.0f, 1.0f}, length, kColorZ);

    buffer_->upload(*device_, vertices_.data(), vertices_.size() * sizeof(Vertex));

    pipeline_->bind(frame.pass);
    wgpuRenderPassEncoderSetBindGroup(frame.pass, 0, frame.globalBindGroup, 0, nullptr);
    buffer_->bind(frame.pass, 0);
    wgpuRenderPassEncoderDraw(frame.pass, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}
