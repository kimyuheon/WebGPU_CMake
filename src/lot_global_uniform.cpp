#include "lot_global_uniform.h"
#include "lot_web_buffer.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

LotGlobalUniform::~LotGlobalUniform() {
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    if (layout_) wgpuBindGroupLayoutRelease(layout_);
}

void LotGlobalUniform::create(lot_web_device& device) {
    if (bindGroup_) return;

    queue_ = device.getQueue();

    // 슬롯이 하나뿐이라 dynamic offset 이 없다
    buffer_ = std::make_unique<lot_web_buffer>(BufferType::UNIFORM, sizeof(Data));
    buffer_->createBuffer(device, nullptr);
    if (!buffer_->isReady()) {
        LOT_ERR("LotGlobalUniform: Failed to create buffer!");
        return;
    }

    // 정점 셰이더는 projection/view 를, 프래그먼트 셰이더는 조명을 읽으므로
    // 두 스테이지 모두에서 보이게 해야 한다.
    WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(Data);

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.label = lotStringView("Global Uniform Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;

    layout_ = wgpuDeviceCreateBindGroupLayout(device.getDevice(), &layoutDesc);
    if (!layout_) {
        LOT_ERR("LotGlobalUniform: Failed to create bind group layout!");
        return;
    }

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer_->getHandle();
    entry.offset = 0;
    entry.size = sizeof(Data);

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.label = lotStringView("Global Uniform Bind Group");
    desc.layout = layout_;
    desc.entryCount = 1;
    desc.entries = &entry;

    bindGroup_ = wgpuDeviceCreateBindGroup(device.getDevice(), &desc);
    if (!bindGroup_) {
        LOT_ERR("LotGlobalUniform: Failed to create bind group!");
        return;
    }

    LOT_LOG("LotGlobalUniform: ready (" << sizeof(Data) << " bytes)");
}

void LotGlobalUniform::update(const LotCamera& camera, const SceneLighting& lighting) {
    if (!isReady()) return;

    const Data data{
        camera.getProjection(),
        camera.getView(),
        {lighting.ambientColor.x, lighting.ambientColor.y, lighting.ambientColor.z,
         lighting.ambientIntensity},
        {lighting.pointLight.position.x, lighting.pointLight.position.y,
         lighting.pointLight.position.z, 0.0f},
        {lighting.pointLight.color.x, lighting.pointLight.color.y,
         lighting.pointLight.color.z, lighting.pointLight.intensity},
    };
    wgpuQueueWriteBuffer(queue_, buffer_->getHandle(), 0, &data, sizeof(data));
}
