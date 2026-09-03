#include "lot_web_pipeline.h"
#include "lot_web_device.h"
#include "lot_web_common.h"
#include "lot_vertex.h"

#include <emscripten/emscripten.h>
#include <iostream>

lot_web_pipeline::lot_web_pipeline(const std::string& shaderPath)
    : shaderPath_(shaderPath) {
    std::cout << "lot_web_pipeline: Constructor (shader: " << shaderPath << ")" << std::endl;
}

lot_web_pipeline::~lot_web_pipeline() {
    std::cout << "lot_web_pipeline: Destructor" << std::endl;
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
    }
}

void lot_web_pipeline::createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat) {
    device_ = device.getDevice();
    colorFormat_ = colorFormat;

    std::cout << "lot_web_pipeline: Loading shader from " << shaderPath_ << std::endl;

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
    std::cerr << "lot_web_pipeline: Failed to load shader: " << self->shaderPath_ << std::endl;
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
        std::cerr << "lot_web_pipeline: Failed to create shader module!" << std::endl;
        return;
    }

    // 2. 정점 레이아웃 - lot_vertex.h 의 Vertex 구조체가 유일한 정의처다.
    //    (예전에는 C++ 72바이트 / JS arrayStride 24 / WGSL 이 따로 놀았다)
    WGPUVertexAttribute attributes[2] = {
        WGPU_VERTEX_ATTRIBUTE_INIT,
        WGPU_VERTEX_ATTRIBUTE_INIT,
    };
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = offsetof(Vertex, position);
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = offsetof(Vertex, color);
    attributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout vertexLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.arrayStride = sizeof(Vertex);
    vertexLayout.attributeCount = 2;
    vertexLayout.attributes = attributes;

    // 3. 프래그먼트 스테이지
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = colorFormat_;

    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = lotStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // 4. 파이프라인
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = lotStringView(shaderPath_);
    desc.layout = nullptr;  // 'auto' 레이아웃 - 셰이더에서 추론
    desc.vertex.module = shaderModule;
    desc.vertex.entryPoint = lotStringView("vs_main");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexLayout;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.fragment = &fragmentState;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &desc);

    wgpuShaderModuleRelease(shaderModule);

    if (!pipeline_) {
        std::cerr << "lot_web_pipeline: Failed to create pipeline!" << std::endl;
        return;
    }

    std::cout << "lot_web_pipeline: Pipeline created successfully!" << std::endl;
}

void lot_web_pipeline::bind(WGPURenderPassEncoder pass) {
    if (!pipeline_ || pass == nullptr) return;
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
}

void lot_web_pipeline::draw(WGPURenderPassEncoder pass, uint32_t vertexCount) {
    if (!pipeline_ || pass == nullptr) return;
    wgpuRenderPassEncoderDraw(pass, vertexCount, 1, 0, 0);
}
