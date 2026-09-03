#include "simple_render_system.h"
#include "lot_web_device.h"
#include "lot_web_common.h"
#include <iostream>

SimpleRenderSystem::SimpleRenderSystem(const std::string& shaderPath) {
    pipeline_ = std::make_unique<lot_web_pipeline>(shaderPath);
}

SimpleRenderSystem::~SimpleRenderSystem() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
    }
}

void SimpleRenderSystem::createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat) {
    pipeline_->createPipeline(device, colorFormat);
    std::cout << "SimpleRenderSystem: Pipeline creation started" << std::endl;
}

void SimpleRenderSystem::createUniformBuffer(lot_web_device& device) {
    if (uniformCreated_ || !pipeline_->isReady()) {
        return;
    }

    queue_ = device.getQueue();

    uniformBuffer_ = std::make_unique<lot_web_buffer>(BufferType::UNIFORM, sizeof(UniformData));
    uniformBuffer_->createBuffer(device, nullptr);
    if (!uniformBuffer_->isReady()) {
        std::cerr << "SimpleRenderSystem: Failed to create uniform buffer!" << std::endl;
        return;
    }

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = uniformBuffer_->getHandle();
    entry.offset = 0;
    entry.size = sizeof(UniformData);

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.label = lotStringView("Uniform Bind Group");
    desc.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline_->getHandle(), 0);
    desc.entryCount = 1;
    desc.entries = &entry;

    bindGroup_ = wgpuDeviceCreateBindGroup(device.getDevice(), &desc);
    wgpuBindGroupLayoutRelease(desc.layout);

    if (!bindGroup_) {
        std::cerr << "SimpleRenderSystem: Failed to create bind group!" << std::endl;
        return;
    }

    uniformCreated_ = true;
    std::cout << "SimpleRenderSystem: Uniform buffer created" << std::endl;
}

void SimpleRenderSystem::renderGameObjects(WGPURenderPassEncoder pass,
                                           std::vector<LotGameObject>& gameObjects) {
    if (!isReady() || pass == nullptr) return;

    // 파이프라인 바인딩
    pipeline_->bind(pass);

    for (auto& obj : gameObjects) {
        // Transform 정보로 uniform 업데이트
        const auto& transform = obj.transform2d;

        UniformData uniform{
            transform.translation.x,
            transform.translation.y,
            transform.rotation,
            transform.scale.x,
        };
        // NOTE: 유니폼 버퍼와 바인드 그룹이 아직 하나뿐이라, 오브젝트가 2개 이상이면
        //       모든 draw 가 마지막에 쓴 transform 을 보게 된다 (writeBuffer 는
        //       submit 시점에 반영되기 때문). 이번 전환 범위에서는 기존 동작을
        //       그대로 유지했고, 다음 단계에서 dynamic offset 으로 고쳐야 한다.
        wgpuQueueWriteBuffer(queue_, uniformBuffer_->getHandle(), 0, &uniform, sizeof(uniform));

        // Bind group 바인딩
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);

        // Vertex 버퍼 바인딩
        if (obj.model != nullptr) {
            obj.model->bind(pass, 0);
        }

        // Draw 호출
        if (obj.vertexCount > 0) {
            pipeline_->draw(pass, obj.vertexCount);
        }
    }
}
