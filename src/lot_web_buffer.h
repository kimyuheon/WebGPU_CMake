#pragma once

#include <webgpu/webgpu.h>
#include <cstddef>

class lot_web_device;

// 버퍼 타입
enum class BufferType {
    VERTEX,
    INDEX,
    UNIFORM
};

class lot_web_buffer {
public:
    lot_web_buffer(BufferType type, size_t size);
    ~lot_web_buffer();

    // 복사 금지
    lot_web_buffer(const lot_web_buffer&) = delete;
    lot_web_buffer& operator=(const lot_web_buffer&) = delete;

    // 버퍼 생성 및 데이터 업로드 (data 가 null 이면 업로드 생략)
    void createBuffer(lot_web_device& device, const void* data);

    // 버퍼 바인딩 (Vertex/Index 버퍼용)
    void bind(WGPURenderPassEncoder pass, uint32_t slot = 0);

    // 정보 가져오기
    BufferType getType() const { return type_; }
    size_t getSize() const { return size_; }
    WGPUBuffer getHandle() const { return buffer_; }
    bool isReady() const { return buffer_ != nullptr; }

private:
    BufferType type_;
    size_t size_;
    WGPUBuffer buffer_ = nullptr;
};
