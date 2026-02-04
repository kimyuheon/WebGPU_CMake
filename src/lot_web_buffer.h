#pragma once

#include <emscripten/emscripten.h>
#include <cstddef>

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

    // 버퍼 생성 및 데이터 업로드
    void createBuffer(const void* data);

    // 버퍼 바인딩 (Vertex/Index 버퍼용)
    void bind(int slot = 0);

    // 정보 가져오기
    BufferType getType() const { return type_; }
    size_t getSize() const { return size_; }
    bool isReady() const;

private:
    BufferType type_;
    size_t size_;
    int bufferId_;  // JavaScript에서 관리하는 버퍼 ID
};
