#include "post_process_system.h"
#include "lot_render_target.h"
#include "lot_web_buffer.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

PostProcessSystem::~PostProcessSystem() {
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (layout_) wgpuBindGroupLayoutRelease(layout_);
}

void PostProcessSystem::create(lot_web_device& device, WGPUTextureFormat colorFormat) {
    if (pipeline_) return;
    device_ = &device;

    // 유니폼 (모드, 선 굵기)
    uniformBuffer_ = std::make_unique<lot_web_buffer>(BufferType::UNIFORM, sizeof(Uniforms));
    uniformBuffer_->createBuffer(device, nullptr);

    // 샘플러 - 화면 크기 그대로 옮기므로 필터는 의미 없지만 Linear 가 기본값이다
    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.label = lotStringView("Post Sampler");
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device.getDevice(), &samplerDesc);

    // 레이아웃: 0 색 텍스처, 1 샘플러, 2 뎁스 텍스처, 3 유니폼
    WGPUBindGroupLayoutEntry entries[4] = {
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    };
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // 뎁스는 Depth 샘플 타입. 셰이더에서 texture_depth_2d 로 받고 textureLoad 로 읽는다.
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].buffer.type = WGPUBufferBindingType_Uniform;
    entries[3].buffer.minBindingSize = sizeof(Uniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Post Layout");
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;
    layout_ = wgpuDeviceCreateBindGroupLayout(device.getDevice(), &layoutDesc);
    if (!layout_) {
        LOT_ERR("PostProcessSystem: Failed to create bind group layout!");
        return;
    }

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.label = lotStringView("Post Pipeline Layout");
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &layout_;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &plDesc);
    if (!pipelineLayout_) {
        LOT_ERR("PostProcessSystem: Failed to create pipeline layout!");
        return;
    }

    // 전체 화면 삼각형: 정점 버퍼 없음, 뎁스 없음 (스왑체인 패스에 뎁스를 안 붙인다)
    PipelineConfig config;
    config.topology = WGPUPrimitiveTopology_TriangleList;
    config.cullMode = WGPUCullMode_None;
    config.useVertexBuffer = false;

    pipeline_ = std::make_unique<lot_web_pipeline>("shaders/post.wgsl");
    pipeline_->createPipeline(device, colorFormat, WGPUTextureFormat_Undefined,
                              pipelineLayout_, config);
    LOT_LOG("PostProcessSystem: created");
}

bool PostProcessSystem::isReady() const {
    return pipeline_ && pipeline_->isReady() && layout_ != nullptr
        && uniformBuffer_ && uniformBuffer_->isReady();
}

void PostProcessSystem::rebuildBindGroup(const LotRenderTarget& target) {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }

    WGPUBindGroupEntry entries[4] = {
        WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT,
    };
    entries[0].binding = 0;
    entries[0].textureView = target.getColorView();
    entries[1].binding = 1;
    entries[1].sampler = sampler_;
    entries[2].binding = 2;
    entries[2].textureView = target.getDepthView();
    entries[3].binding = 3;
    entries[3].buffer = uniformBuffer_->getHandle();
    entries[3].offset = 0;
    entries[3].size = sizeof(Uniforms);

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.label = lotStringView("Post Bind Group");
    desc.layout = layout_;
    desc.entryCount = 4;
    desc.entries = entries;
    bindGroup_ = wgpuDeviceCreateBindGroup(device_->getDevice(), &desc);

    boundColor_ = target.getColorView();
    boundDepth_ = target.getDepthView();
}

void PostProcessSystem::render(WGPURenderPassEncoder pass, const LotRenderTarget& target) {
    if (!isReady() || pass == nullptr || !target.isReady()) return;

    // 타깃이 다시 만들어졌으면(리사이즈) 뷰가 바뀌어 바인드 그룹도 새로 만든다
    if (!bindGroup_ || boundColor_ != target.getColorView()
        || boundDepth_ != target.getDepthView()) {
        rebuildBindGroup(target);
        if (!bindGroup_) return;
    }

    const Uniforms uniforms{static_cast<uint32_t>(mode), outlineWidth, 0, 0};
    wgpuQueueWriteBuffer(device_->getQueue(), uniformBuffer_->getHandle(), 0,
                         &uniforms, sizeof(uniforms));

    pipeline_->bind(pass);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);  // 삼각형 하나
}
