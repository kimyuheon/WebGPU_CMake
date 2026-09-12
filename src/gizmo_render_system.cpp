#include "gizmo_render_system.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

#include <cmath>
#include <limits>

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
    config.alphaBlend = true;             // 평면 핸들이 반투명

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
        vertices_.push_back(Vertex::make(p.x, p.y, p.z,
                                         color.x, color.y, color.z,
                                         0.0f, -1.0f, 0.0f));
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

void GizmoRenderSystem::buildPlane(const vec3& origin, int normalAxis, float length,
                                   const vec3& color, float alpha) {
    // 법선이 n 축이면 평면은 나머지 두 축이 펼친다
    const vec3 a = axisDirection((normalAxis + 1) % 3);
    const vec3 b = axisDirection((normalAxis + 2) % 3);
    const float lo = length * planeInner;
    const float hi = length * planeOuter;

    const vec3 p00 = origin + a * lo + b * lo;
    const vec3 p10 = origin + a * hi + b * lo;
    const vec3 p11 = origin + a * hi + b * hi;
    const vec3 p01 = origin + a * lo + b * hi;

    auto push = [&](const vec3& p) {
        Vertex v = Vertex::make(p.x, p.y, p.z, color.x, color.y, color.z, 0.0f, -1.0f, 0.0f);
        v.color[3] = alpha;
        vertices_.push_back(v);
    };
    // 컬링이 꺼져 있으므로 감는 방향은 상관없다
    push(p00); push(p10); push(p11);
    push(p00); push(p11); push(p01);
}

bool GizmoRenderSystem::intersectPlane(const lot_pick::Ray& ray, const vec3& position,
                                       int normalAxis, vec3& hitOut) {
    const vec3 n = axisDirection(normalAxis);
    const float denom = dot(ray.direction, n);
    if (std::fabs(denom) < 1e-6f) return false;  // 평행 - 평면을 옆에서 보고 있다

    const float t = dot(position - ray.origin, n) / denom;
    if (t < 0.0f) return false;  // 카메라 뒤
    hitOut = ray.origin + ray.direction * t;
    return true;
}

vec3 GizmoRenderSystem::axisDirection(int axis) {
    switch (axis) {
        case 0: return vec3{1.0f, 0.0f, 0.0f};
        case 1: return vec3{0.0f, 1.0f, 0.0f};  // +Y 는 아래쪽이다
        default: return vec3{0.0f, 0.0f, 1.0f};
    }
}

float GizmoRenderSystem::arrowLength(const LotCamera& camera, const vec3& position) const {
    if (camera.isOrthographic()) {
        // 직교에서는 거리와 무관하게 화면 세로의 일정 비율로 보이게 한다.
        // halfHeight 가 화면 절반에 담기는 월드 길이이므로 그 비율로 잡으면 된다.
        return std::fmax(camera.getOrthoHalfHeight() * screenScale * 2.0f, 0.05f);
    }
    // 원근: 카메라 거리에 비례해 키운다. 원근 나눗셈이 1/거리로 줄이는 것을 상쇄한다.
    const vec3 toCamera = camera.getPosition() - position;
    const float distance = std::sqrt(dot(toCamera, toCamera));
    return std::fmax(distance * screenScale, 0.05f);
}

int GizmoRenderSystem::hitTest(const lot_pick::Ray& ray, const LotCamera& camera,
                               const vec3& position) const {
    const float length = arrowLength(camera, position);

    // 1. 평면 핸들부터. 레이-평면 교점이 사각형 안이면 집은 것이다.
    //    교점을 두 축에 투영한 값이 [inner, outer] 안이어야 한다.
    //
    //    두 가지를 더 본다:
    //    - 거의 옆에서 보는 평면은 뺀다. 화면에서 선 하나로 보여 집을 수 없고,
    //      집혔다 해도 화면의 1px 이 월드에서 한참이라 드래그가 튄다.
    //    - 여럿이 맞으면 레이에서 가장 가까운 것. 앞에 보이는 게 클릭한 것이다.
    constexpr float kMinFacing = 0.25f;  // |cos| 이 이보다 작으면 옆에서 보는 것
    int bestPlane = -1;
    float bestT = std::numeric_limits<float>::max();
    for (int n = 0; n < 3; ++n) {
        const vec3 normal = axisDirection(n);
        if (std::fabs(dot(ray.direction, normal)) < kMinFacing) continue;

        vec3 hit;
        if (!intersectPlane(ray, position, n, hit)) continue;
        const vec3 rel = hit - position;
        const float u = dot(rel, axisDirection((n + 1) % 3));
        const float v = dot(rel, axisDirection((n + 2) % 3));
        const float lo = length * planeInner;
        const float hi = length * planeOuter;
        if (u < lo || u > hi || v < lo || v > hi) continue;

        const float t = dot(hit - ray.origin, ray.direction);
        if (t < bestT) {
            bestT = t;
            bestPlane = 3 + n;
        }
    }
    if (bestPlane >= 0) return bestPlane;

    // 2. 축 화살표. 레이와 선분 사이 거리로.
    const float tolerance = length * pickTolerance;
    int best = -1;
    float bestDistance = tolerance;
    for (int axis = 0; axis < 3; ++axis) {
        float s = 0.0f;
        const vec3 tip = position + axisDirection(axis) * length;
        const float d = lot_pick::distanceRayToSegment(ray, position, tip, s);
        if (d < bestDistance) {
            bestDistance = d;
            best = axis;
        }
    }
    return best;
}

void GizmoRenderSystem::render(FrameInfo& frame, const vec3& position, int highlight) {
    if (!isReady() || frame.pass == nullptr || device_ == nullptr) return;

    const float length = arrowLength(frame.camera, position);

    // 끌고 있는 핸들은 흰색에 가깝게 밝힌다
    auto colorFor = [&](int handle, const vec3& base) {
        if (handle != highlight) return base;
        return vec3{base.x * 0.4f + 0.6f, base.y * 0.4f + 0.6f, base.z * 0.4f + 0.6f};
    };
    const vec3 axisColors[3] = {kColorX, kColorY, kColorZ};

    vertices_.clear();
    // 평면 핸들은 법선 축의 색을 쓴다 (XY 평면 = Z 색). 끌고 있으면 더 진하게.
    for (int n = 0; n < 3; ++n) {
        const float alpha = (3 + n == highlight) ? 0.75f : 0.35f;
        buildPlane(position, n, length, colorFor(3 + n, axisColors[n]), alpha);
    }
    for (int axis = 0; axis < 3; ++axis) {
        buildArrow(position, axisDirection(axis), length, colorFor(axis, axisColors[axis]));
    }

    buffer_->upload(*device_, vertices_.data(), vertices_.size() * sizeof(Vertex));

    pipeline_->bind(frame.pass);
    wgpuRenderPassEncoderSetBindGroup(frame.pass, 0, frame.globalBindGroup, 0, nullptr);
    buffer_->bind(frame.pass, 0);
    wgpuRenderPassEncoderDraw(frame.pass, static_cast<uint32_t>(vertices_.size()), 1, 0, 0);
}
