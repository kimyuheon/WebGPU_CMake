#include "simple_render_system.h"
#include "lot_model.h"
#include "lot_web_device.h"
#include "lot_web_common.h"
#include "lot_log.h"

SimpleRenderSystem::SimpleRenderSystem(const std::string& shaderPath) {
    pipeline_ = std::make_unique<lot_web_pipeline>(shaderPath);
}

SimpleRenderSystem::~SimpleRenderSystem() {
    if (globalBindGroup_) wgpuBindGroupRelease(globalBindGroup_);
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (globalBindGroupLayout_) wgpuBindGroupLayoutRelease(globalBindGroupLayout_);
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
        LOT_ERR("SimpleRenderSystem: Failed to create uniform buffer!");
        return;
    }

    // 2-1. 프레임당 유니폼 (카메라 + 조명). 슬롯이 하나뿐이라 dynamic offset 이 없다.
    globalBuffer_ = std::make_unique<lot_web_buffer>(
        BufferType::UNIFORM, sizeof(GlobalUniformData));
    globalBuffer_->createBuffer(device, nullptr);
    if (!globalBuffer_->isReady()) {
        LOT_ERR("SimpleRenderSystem: Failed to create global uniform buffer!");
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
        LOT_ERR("SimpleRenderSystem: Failed to create bind group layout!");
        return;
    }

    // 3-1. 프레임당 유니폼 레이아웃.
    //      정점 셰이더는 projection/view 를, 프래그먼트 셰이더는 조명을 읽으므로
    //      두 스테이지 모두에서 보이게 해야 한다.
    WGPUBindGroupLayoutEntry globalLayoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    globalLayoutEntry.binding = 0;
    globalLayoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    globalLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    globalLayoutEntry.buffer.minBindingSize = sizeof(GlobalUniformData);

    WGPUBindGroupLayoutDescriptor globalLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    globalLayoutDesc.label = lotStringView("Global Uniform Layout");
    globalLayoutDesc.entryCount = 1;
    globalLayoutDesc.entries = &globalLayoutEntry;

    globalBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device.getDevice(), &globalLayoutDesc);
    if (!globalBindGroupLayout_) {
        LOT_ERR("SimpleRenderSystem: Failed to create global bind group layout!");
        return;
    }

    // 4. 파이프라인 레이아웃 (Vulkan 쪽 pipelineLayout 과 같은 역할).
    //    배열 순서가 곧 셰이더의 @group 번호다.
    WGPUBindGroupLayout layouts[2] = {globalBindGroupLayout_, bindGroupLayout_};

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.label = lotStringView("Simple Render System Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = layouts;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device.getDevice(), &pipelineLayoutDesc);
    if (!pipelineLayout_) {
        LOT_ERR("SimpleRenderSystem: Failed to create pipeline layout!");
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
        LOT_ERR("SimpleRenderSystem: Failed to create bind group!");
        return;
    }

    // 5-1. 프레임당 바인드 그룹
    WGPUBindGroupEntry globalEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    globalEntry.binding = 0;
    globalEntry.buffer = globalBuffer_->getHandle();
    globalEntry.offset = 0;
    globalEntry.size = sizeof(GlobalUniformData);

    WGPUBindGroupDescriptor globalDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    globalDesc.label = lotStringView("Global Uniform Bind Group");
    globalDesc.layout = globalBindGroupLayout_;
    globalDesc.entryCount = 1;
    globalDesc.entries = &globalEntry;

    globalBindGroup_ = wgpuDeviceCreateBindGroup(device.getDevice(), &globalDesc);
    if (!globalBindGroup_) {
        LOT_ERR("SimpleRenderSystem: Failed to create global bind group!");
        return;
    }

    uniformCreated_ = true;
    LOT_LOG("SimpleRenderSystem: Uniform ready (stride " << uniformStride_
              << " bytes, " << kMaxObjects << " slots)");
}

void SimpleRenderSystem::createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat,
                                        WGPUTextureFormat depthFormat) {
    if (!pipelineLayout_) {
        LOT_ERR("SimpleRenderSystem: createUniformBuffer must run first!");
        return;
    }
    pipeline_->createPipeline(device, colorFormat, depthFormat, pipelineLayout_);
    LOT_LOG("SimpleRenderSystem: Pipeline creation started");
}

void SimpleRenderSystem::renderGameObjects(WGPURenderPassEncoder pass,
                                           std::vector<LotGameObject>& gameObjects,
                                           const LotCamera& camera,
                                           const SceneLighting& lighting) {
    if (!isReady() || pass == nullptr) return;

    // 파이프라인 바인딩
    pipeline_->bind(pass);

    // 1. 프레임당 유니폼 - 카메라와 조명. 오브젝트 수와 무관하게 한 번만 쓴다.
    const GlobalUniformData global{
        camera.getProjection(),
        camera.getView(),
        {lighting.ambientColor.x, lighting.ambientColor.y, lighting.ambientColor.z,
         lighting.ambientIntensity},
        {lighting.pointLight.position.x, lighting.pointLight.position.y,
         lighting.pointLight.position.z, 0.0f},
        {lighting.pointLight.color.x, lighting.pointLight.color.y,
         lighting.pointLight.color.z, lighting.pointLight.intensity},
    };
    wgpuQueueWriteBuffer(queue_, globalBuffer_->getHandle(), 0, &global, sizeof(global));
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup_, 0, nullptr);

    // 2. 오브젝트별 유니폼
    uint32_t slot = 0;
    for (auto& obj : gameObjects) {
        if (slot >= kMaxObjects) {
            if (!overflowWarned_) {
                LOT_ERR("SimpleRenderSystem: more than " << kMaxObjects
              << " objects, extras are skipped");
                overflowWarned_ = true;
            }
            break;
        }

        // 오브젝트마다 자기 슬롯에 transform 을 쓴다.
        // 예전에는 슬롯이 하나뿐이라 모든 draw 가 마지막 값을 봤다
        // (writeBuffer 는 submit 시점에 반영되므로).
        //
        // 예전에는 여기서 projection * view 까지 미리 곱해 보냈지만, 점 광원은
        // 조각의 월드 좌표가 필요해서 셰이더가 model 을 따로 알아야 한다.
        //
        // 노멀은 모델 행렬로 변환하면 안 된다 - 축마다 다른 스케일이 방향을
        // 틀어놓기 때문이다. 그래서 별도로 하나 더 보낸다.
        const UniformData uniform{
            obj.transform.mat4Transform(),
            obj.transform.normalMatrix(),
        };

        const uint32_t byteOffset = slot * uniformStride_;
        wgpuQueueWriteBuffer(queue_, uniformBuffer_->getHandle(), byteOffset,
                             &uniform, sizeof(uniform));

        // dynamic offset 으로 이 오브젝트의 슬롯을 가리킨다
        wgpuRenderPassEncoderSetBindGroup(pass, 1, bindGroup_, 1, &byteOffset);

        // 정점/인덱스 버퍼 바인딩과 draw 는 모델이 알아서 한다
        if (obj.model) {
            obj.model->bind(pass);
            obj.model->draw(pass);
        }

        ++slot;
    }
}
