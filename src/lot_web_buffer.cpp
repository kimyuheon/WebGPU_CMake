#include "lot_web_buffer.h"
#include "lot_web_device.h"
#include "lot_web_common.h"
#include <iostream>

namespace {

const char* typeName(BufferType type) {
    switch (type) {
        case BufferType::VERTEX:  return "Vertex";
        case BufferType::INDEX:   return "Index";
        case BufferType::UNIFORM: return "Uniform";
    }
    return "Unknown";
}

WGPUBufferUsage typeUsage(BufferType type) {
    switch (type) {
        case BufferType::VERTEX:  return WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        case BufferType::INDEX:   return WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        case BufferType::UNIFORM: return WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    }
    return WGPUBufferUsage_None;
}

}  // namespace

lot_web_buffer::lot_web_buffer(BufferType type, size_t size)
    : type_(type), size_(size) {
    std::cout << "lot_web_buffer: Constructor (" << typeName(type)
              << ", " << size << " bytes)" << std::endl;
}

lot_web_buffer::~lot_web_buffer() {
    std::cout << "lot_web_buffer: Destructor (" << typeName(type_) << ")" << std::endl;
    if (buffer_) {
        wgpuBufferDestroy(buffer_);
        wgpuBufferRelease(buffer_);
    }
}

void lot_web_buffer::createBuffer(lot_web_device& device, const void* data) {
    if (buffer_) {
        return;  // 이미 생성됨
    }

    std::string label = std::string(typeName(type_)) + " Buffer";

    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = lotStringView(label);
    desc.usage = typeUsage(type_);
    desc.size = size_;

    buffer_ = wgpuDeviceCreateBuffer(device.getDevice(), &desc);
    if (!buffer_) {
        std::cerr << "lot_web_buffer: Failed to create buffer!" << std::endl;
        return;
    }

    if (data != nullptr) {
        wgpuQueueWriteBuffer(device.getQueue(), buffer_, 0, data, size_);
    }

    std::cout << "lot_web_buffer: Created " << typeName(type_)
              << " buffer (" << size_ << " bytes)" << std::endl;
}

void lot_web_buffer::bind(WGPURenderPassEncoder pass, uint32_t slot) {
    if (!buffer_ || pass == nullptr) {
        return;
    }

    if (type_ == BufferType::VERTEX) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, slot, buffer_, 0, size_);
    } else if (type_ == BufferType::INDEX) {
        wgpuRenderPassEncoderSetIndexBuffer(pass, buffer_, WGPUIndexFormat_Uint32, 0, size_);
    }
}
