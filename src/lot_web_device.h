#pragma once

#include <emscripten/emscripten.h>

class lot_web_device {
public:
    lot_web_device();
    ~lot_web_device();

    // 초기화 (비동기)
    bool init();

    // 초기화 완료 확인
    bool isInitialized() const;

private:
    bool initialized_ = false;
};
