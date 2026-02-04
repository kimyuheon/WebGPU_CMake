#include "lot_web_buffer.h"
#include <iostream>

// 다음 버퍼 ID (C++에서 추적)
static int g_nextBufferId = 1;

// JavaScript 함수 선언 (webgpu_bindings.js에서 구현)
extern "C" {
    extern void js_createBuffer(int bufferId, int bufferType, const void* data, size_t size);
    extern bool js_isBufferReady(int bufferId);
    extern void js_bindVertexBuffer(int bufferId, int slot);
    extern void js_bindIndexBuffer(int bufferId);
    extern void js_destroyBuffer(int bufferId);
}

// C++ 구현
lot_web_buffer::lot_web_buffer(BufferType type, size_t size)
    : type_(type), size_(size), bufferId_(g_nextBufferId++) {
    const char* typeName =
        (type == BufferType::VERTEX) ? "Vertex" :
        (type == BufferType::INDEX) ? "Index" : "Uniform";
    std::cout << "lot_web_buffer: Constructor (" << typeName << ", " << size << " bytes)" << std::endl;
}

lot_web_buffer::~lot_web_buffer() {
    std::cout << "lot_web_buffer: Destructor (ID: " << bufferId_ << ")" << std::endl;
    if (isReady()) {
        js_destroyBuffer(bufferId_);
    }
}

void lot_web_buffer::createBuffer(const void* data) {
    std::cout << "lot_web_buffer: Creating buffer (ID: " << bufferId_ << ")..." << std::endl;
    js_createBuffer(bufferId_, static_cast<int>(type_), data, size_);
}

void lot_web_buffer::bind(int slot) {
    if (!isReady()) return;

    if (type_ == BufferType::VERTEX) {
        js_bindVertexBuffer(bufferId_, slot);
    } else if (type_ == BufferType::INDEX) {
        js_bindIndexBuffer(bufferId_);
    }
}

bool lot_web_buffer::isReady() const {
    return js_isBufferReady(bufferId_);
}
