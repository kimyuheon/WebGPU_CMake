#include "lot_dynamic_buffer.h"
#include "lot_web_device.h"
#include "lot_log.h"

#include <cstring>
#include <vector>

namespace {

// 처음 잡는 용량. 너무 작으면 초반에 재할당이 잦고, 너무 크면 낭비다.
constexpr size_t kInitialCapacity = 4096;

}  // namespace

void LotDynamicBuffer::upload(lot_web_device& device, const void* data, size_t bytes) {
    // WebGPU 버퍼 크기와 writeBuffer 크기는 4 의 배수여야 한다
    const size_t padded = (bytes + 3) & ~static_cast<size_t>(3);

    if (!buffer_ || padded > capacity_) {
        size_t newCapacity = (capacity_ == 0) ? kInitialCapacity : capacity_;
        while (newCapacity < padded) newCapacity *= 2;

        // 옛 버퍼는 unique_ptr 이 놓는 순간 destroy 된다.
        // 이전 프레임의 명령은 이미 제출됐으므로 큐 순서상 먼저 끝난다.
        buffer_ = std::make_unique<lot_web_buffer>(type_, newCapacity);
        buffer_->createBuffer(device, nullptr);
        if (!buffer_->isReady()) {
            LOT_ERR("LotDynamicBuffer: failed to grow to " << newCapacity << " bytes");
            capacity_ = 0;
            size_ = 0;
            return;
        }
        capacity_ = newCapacity;
        LOT_LOG("LotDynamicBuffer: capacity " << capacity_ << " bytes");
    }

    if (bytes > 0) {
        if (padded == bytes) {
            wgpuQueueWriteBuffer(device.getQueue(), buffer_->getHandle(), 0, data, bytes);
        } else {
            // 4 의 배수가 아니면 끝을 0 으로 채운 사본을 올린다.
            // data 뒤를 읽으면 안 되므로 그냥 padded 로 쓸 수는 없다.
            std::vector<unsigned char> copy(padded, 0);
            std::memcpy(copy.data(), data, bytes);
            wgpuQueueWriteBuffer(device.getQueue(), buffer_->getHandle(), 0,
                                 copy.data(), padded);
        }
    }
    size_ = bytes;
}

void LotDynamicBuffer::bind(WGPURenderPassEncoder pass, uint32_t slot) {
    if (!isReady() || pass == nullptr) return;
    buffer_->bind(pass, slot);
}
