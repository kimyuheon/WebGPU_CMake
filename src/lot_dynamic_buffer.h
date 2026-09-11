#pragma once

#include "lot_web_buffer.h"

#include <webgpu/webgpu.h>
#include <cstddef>
#include <memory>

class lot_web_device;

// 내용이 프레임마다 바뀌는 버퍼.
//
// lot_web_buffer 는 크기가 고정이라 (메시처럼 한 번 올리고 끝인 데이터용),
// 선/폴리라인/기즈모처럼 개수가 매 프레임 달라지는 데이터에는 맞지 않는다.
// 이 클래스는 용량을 넉넉히 잡아두고 writeBuffer 로 덮어쓰다가,
// 모자라면 두 배로 다시 만든다. std::vector 와 같은 전략이다.
class LotDynamicBuffer {
public:
    explicit LotDynamicBuffer(BufferType type) : type_(type) {}

    // data 를 GPU 에 올린다. 빈 데이터(bytes == 0)도 허용한다 - 그릴 게 없는 프레임.
    //
    // 버퍼를 새로 만드는 도중에 이전 프레임이 옛 버퍼를 아직 쓰고 있어도 괜찮다.
    // WebGPU 는 큐 순서를 보장하므로, 제출된 명령이 끝난 뒤에야 파괴가 반영된다.
    void upload(lot_web_device& device, const void* data, size_t bytes);

    // 정점 버퍼로 묶는다 (BufferType::VERTEX 일 때).
    void bind(WGPURenderPassEncoder pass, uint32_t slot = 0);

    // 마지막 upload 로 올라간 바이트 수. 용량과는 다르다.
    size_t getSize() const { return size_; }
    bool isReady() const { return buffer_ && buffer_->isReady(); }

private:
    BufferType type_;
    std::unique_ptr<lot_web_buffer> buffer_;
    size_t capacity_ = 0;
    size_t size_ = 0;
};
