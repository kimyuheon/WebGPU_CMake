#include "simple_render_system.h"
#include "lot_model.h"
#include "lot_web_device.h"
#include "lot_web_common.h"
#include <iostream>

SimpleRenderSystem::SimpleRenderSystem(const std::string& shaderPath) {
    pipeline_ = std::make_unique<lot_web_pipeline>(shaderPath);
}

SimpleRenderSystem::~SimpleRenderSystem() {
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (bindGroupLayout_) wgpuBindGroupLayoutRelease(bindGroupLayout_);
}

void SimpleRenderSystem::createUniformBuffer(lot_web_device& device) {
    if (uniformCreated_) {
        return;
    }

    queue_ = device.getQueue();

    // 1. dynamic offset 은 minUniformBufferOffsetAlignment 배수여야 한다.
    //    오브젝트 하나당 이 간격만큼 슬롯을 잡는다.
    WGPULimits limits = WGPU_LIMITS_INIT;
    uint32_t alignment = 256;  // 스펙상 최대값 - 조회 실패 시 안전한 기본값
    if (wgpuDeviceGetLimits(device.getDevice(), &limits) == WGPUStatus_Success
        && limits.minUniformBufferOffsetAlignment != WGPU_LIMIT_U32_UNDEFINED) {
        alignment = limits.minUniformBufferOffsetAlignment;
    }
    uniformStride_ = ((sizeof(UniformData) + alignment - 1) / alignment) * alignment;

    // 2. 오브젝트 수만큼 슬롯을 가진 하나의 큰 uniform 버퍼
    uniformBuffer_ = std::make_unique<lot_web_buffer>(
        BufferType::UNIFORM, static_cast<size_t>(uniformStride_) * kMaxObjects);
    uniformBuffer_->createBuffer(device, nullptr);
    if (!uniformBuffer_->isReady()) {
        std::cerr << "SimpleRenderSystem: Failed to create uniform buffer!" << std::endl;
        return;
    }

    // 3. 바인드 그룹 레이아웃 - hasDynamicOffset 을 켜는 것이 이번 변경의 핵심.
    //    'auto' 레이아웃으로는 이 플래그를 켤 수 없어서 직접 만든다.
    WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.hasDynamicOffset = WGPU_TRUE;
    layoutEntry.buffer.minBindingSize = sizeof(UniformData);

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Object Uniform Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;

    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device.getDevice(), &layoutDesc);
    if (!bindGroupLayout_) {
        std::cerr << "SimpleRenderSystem: Failed to create bind group layout!" << std::endl;
        return;
    }

    // 4. 파이프라인 레이아웃 (Vulkan 쪽 pipelineLayout 과 같은 역할)
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.label = lotStringView("Simple Render System Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout_;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &pipelineLayoutDesc);
    if (!pipelineLayout_) {
        std::cerr << "SimpleRenderSystem: Failed to create pipeline layout!" << std::endl;
        return;
    }

    // 5. 바인드 그룹은 하나면 된다.
    //    size 를 UniformData 하나 크기로 잡아두고, dynamic offset 으로 창을 옮긴다.
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = uniformBuffer_->getHandle();
    entry.offset = 0;
    entry.size = sizeof(UniformData);

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.label = lotStringView("Object Uniform Bind Group");
    desc.layout = bindGroupLayout_;
    desc.entryCount = 1;
    desc.entries = &entry;

    bindGroup_ = wgpuDeviceCreateBindGroup(device.getDevice(), &desc);
    if (!bindGroup_) {
        std::cerr << "SimpleRenderSystem: Failed to create bind group!" << std::endl;
        return;
    }

    uniformCreated_ = true;
    std::cout << "SimpleRenderSystem: Uniform ready (stride " << uniformStride_
              << " bytes, " << kMaxObjects << " slots)" << std::endl;
}

void SimpleRenderSystem::createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat,
                                        WGPUTextureFormat depthFormat) {
    if (!pipelineLayout_) {
        std::cerr << "SimpleRenderSystem: createUniformBuffer must run first!" << std::endl;
        return;
    }
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_);
    std::cout << "SimpleRenderSystem: Pipeline creation started" << std::endl;
}

void SimpleRenderSystem::renderGameObjects(WGPURenderPassEncoder pass,
                                           std::vector<LotGameObject>& gameObjects,
                                           const LotCamera& camera) {
    if (!isReady() || pass == nullptr) return;

    // 파이프라인 바인딩
    pipeline_->bind(pass);

    // 카메라 쪽 두 행렬은 프레임당 한 번만 곱하면 된다
    const mat4 projectionView = camera.getProjectionView();

    uint32_t slot = 0;
    for (auto& obj : gameObjects) {
        if (slot >= kMaxObjects) {
            if (!overflowWarned_) {
                std::cerr << "SimpleRenderSystem: more than " << kMaxObjects
                          << " objects, extras are skipped" << std::endl;
                overflowWarned_ = true;
            }
            break;
        }

        // 오브젝트마다 자기 슬롯에 transform 을 쓴다.
        // 예전에는 슬롯이 하나뿐이라 모든 draw 가 마지막 값을 봤다
        // (writeBuffer 는 submit 시점에 반영되므로).
        // projection * view * model 을 CPU 에서 한 행렬로 접어 보낸다.
        // 셰이더는 정점마다 곱셈 한 번만 하면 된다.
        const UniformData uniform{projectionView * obj.transform.mat4Transform()};

        const uint32_t byteOffset = slot * uniformStride_;
        wgpuQueueWriteBuffer(queue_, uniformBuffer_->getHandle(), byteOffset,
                             &uniform, sizeof(uniform));

        // dynamic offset 으로 이 오브젝트의 슬롯을 가리킨다
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 1, &byteOffset);

        // 정점/인덱스 버퍼 바인딩과 draw 는 모델이 알아서 한다
        if (obj.model) {
            obj.model->bind(pass);
            obj.model->draw(pass);
        }

        ++slot;
    }
}
