#include "lot_material.h"
#include "lot_web_common.h"
#include "lot_web_device.h"
#include "lot_log.h"

WGPUBindGroupLayout LotMaterial::createBindGroupLayout(lot_web_device& device) {
    // binding 0: 텍스처, binding 1: 샘플러. 프래그먼트에서만 읽는다.
    WGPUBindGroupLayoutEntry entries[2] = {
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    };
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[0].texture.multisampled = WGPU_FALSE;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.label = lotStringView("Material Layout");
    desc.entryCount = 2;
    desc.entries = entries;

    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device.getDevice(), &desc);
    if (!layout) {
        LOT_ERR("LotMaterial: failed to create bind group layout");
    }
    return layout;
}

LotMaterial::LotMaterial(lot_web_device& device, WGPUBindGroupLayout layout,
                         std::shared_ptr<LotTexture> texture)
    : texture_(std::move(texture)) {
    if (layout == nullptr || !texture_ || !texture_->isReady()) {
        LOT_ERR("LotMaterial: layout or texture missing");
        return;
    }

    WGPUBindGroupEntry entries[2] = {
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    };
    entries[0].binding = 0;
    entries[0].textureView = texture_->getView();
    entries[1].binding = 1;
    entries[1].sampler = texture_->getSampler();

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.label = lotStringView("Material Bind Group");
    desc.layout = layout;
    desc.entryCount = 2;
    desc.entries = entries;

    bindGroup_ = wgpuDeviceCreateBindGroup(device.getDevice(), &desc);
    if (!bindGroup_) {
        LOT_ERR("LotMaterial: failed to create bind group");
    }
}

LotMaterial::~LotMaterial() {
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
}
