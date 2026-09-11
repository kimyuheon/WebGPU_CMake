#include "lot_web_pipeline.h"
#include "lot_web_device.h"
#include "lot_web_common.h"
#include "lot_vertex.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>

lot_web_pipeline::lot_web_pipeline(const std::string& shaderPath)
    : shaderPath_(shaderPath) {
    LOT_LOG("lot_web_pipeline: Constructor (shader: " << shaderPath << ")");
}

lot_web_pipeline::~lot_web_pipeline() {
    LOT_LOG("lot_web_pipeline: Destructor");
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
    }
}

void lot_web_pipeline::createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat,
                                      WGPUTextureFormat depthFormat, WGPUPipelineLayout layout,
                                      const PipelineConfig& config) {
    device_ = device.getDevice();
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;
    layout_ = layout;
    config_ = config;

    LOT_LOG("lot_web_pipeline: Loading shader from " << shaderPath_);

    // 셰이더 파일을 비동기로 받아온다 (예전 JS 의 fetch 에 해당)
    emscripten_async_wget_data(shaderPath_.c_str(), this, onShaderLoaded, onShaderFailed);
}

void lot_web_pipeline::onShaderLoaded(void* arg, void* buffer, int size) {
    auto* self = static_cast<lot_web_pipeline*>(arg);
    // wget_data 의 버퍼는 널 종료가 아니므로 길이를 명시해 복사한다.
    self->build(std::string(static_cast<const char*>(buffer), static_cast<size_t>(size)));
}

void lot_web_pipeline::onShaderFailed(void* arg) {
    auto* self = static_cast<lot_web_pipeline*>(arg);
    LOT_ERR("lot_web_pipeline: Failed to load shader: " << self->shaderPath_);
}

void lot_web_pipeline::build(const std::string& shaderCode) {
    // 1. 셰이더 모듈
    WGPUShaderSourceWGSL wgslSource = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgslSource.code = lotStringView(shaderCode);

    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgslSource.chain;
    moduleDesc.label = lotStringView(shaderPath_);

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
    if (!shaderModule) {
        LOT_ERR("lot_web_pipeline: Failed to create shader module!");
        return;
    }

    // 2. 정점 레이아웃 - lot_vertex.h 의 Vertex 구조체가 유일한 정의처다.
    //    (예전에는 C++ 72바이트 / JS arrayStride 24 / WGSL 이 따로 놀았다)
    WGPUVertexAttribute attributes[3] = {
        WGPU_VERTEX_ATTRIBUTE_INIT,
        WGPU_VERTEX_ATTRIBUTE_INIT,
        WGPU_VERTEX_ATTRIBUTE_INIT,
    };
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = offsetof(Vertex, color);
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x3;
    attributes[2].offset = offsetof(Vertex, normal);
    attributes[2].shaderLocation = 2;

    WGPUVertexBufferLayout vertexLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.arrayStride = sizeof(Vertex);
    vertexLayout.attributeCount = 3;
    vertexLayout.attributes = attributes;

    // 3. 프래그먼트 스테이지
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = colorFormat_;

    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = lotStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // 4. 뎁스 상태 - 렌더 패스의 뎁스 어태치먼트와 포맷이 같아야 한다.
    //    Less: 더 가까운(z 가 작은) 조각만 통과. WebGPU 의 깊이 범위는 [0, 1] 이다.
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = depthFormat_;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Less;

    // 5. 파이프라인
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = lotStringView(shaderPath_);
    desc.layout = layout_;  // 렌더 시스템이 만든 레이아웃 (dynamic offset 포함)
    desc.vertex.module = shaderModule;
    desc.vertex.entryPoint = lotStringView("vs_main");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexLayout;
    desc.primitive.topology = config_.topology;
    // 스트립(LineStrip/TriangleStrip)은 인덱스 버퍼로 그릴 때 인덱스 형식을
    // 미리 알려줘야 한다. 그래야 0xFFFFFFFF 가 '여기서 끊고 새로 시작' 으로
    // 해석된다 (primitive restart). 우리 인덱스는 전부 uint32 다.
    if (config_.topology == WGPUPrimitiveTopology_LineStrip
        || config_.topology == WGPUPrimitiveTopology_TriangleStrip) {
        desc.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
    }
    // 백페이스 컬링. 뒤통수를 보이는 면은 래스터라이즈 전에 버려진다.
    //
    // 규약: 메시의 삼각형은 오른손 법칙 법선이 바깥을 향하도록 감는다
    // (LotModel::createCube 참고). 우리 좌표계(+Y 아래, +Z 화면 안쪽)와
    // 투영 행렬의 Y 뒤집기까지 거치면 그런 삼각형이 CCW 로 판정된다.
    //
    // 부호를 손으로 따라가면 틀리기 쉬운 자리다. 반대로 넣으면 정육면체의
    // '안쪽'이 보인다 - 정면 대신 뒷면 색이 화면을 채우면 이 값을 의심할 것.
    // 선분에는 앞뒤가 없으므로 그리드/선 파이프라인은 cullMode 를 None 으로 준다.
    desc.primitive.frontFace = WGPUFrontFace_CCW;
    desc.primitive.cullMode = config_.cullMode;
    desc.fragment = &fragmentState;
    if (depthFormat_ != WGPUTextureFormat_Undefined) {
        desc.depthStencil = &depthStencil;
    }

    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &desc);

    wgpuShaderModuleRelease(shaderModule);

    if (!pipeline_) {
        LOT_ERR("lot_web_pipeline: Failed to create pipeline!");
        return;
    }

    LOT_LOG("lot_web_pipeline: Pipeline created successfully!");
}

void lot_web_pipeline::bind(WGPURenderPassEncoder pass) {
    if (!pipeline_ || pass == nullptr) return;
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
}
