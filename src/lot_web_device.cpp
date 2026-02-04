#include "lot_web_device.h"
#include <iostream>

// JavaScript 함수 선언 (webgpu_bindings.js에서 구현)
extern "C" {
    extern void js_initWebGPU();
    extern bool js_isInitialized();
    extern void js_cleanup();
}

// C++ 구현
lot_web_device::lot_web_device() {
    std::cout << "lot_web_device: Constructor" << std::endl;
}

lot_web_device::~lot_web_device() {
    std::cout << "lot_web_device: Destructor" << std::endl;
    if (initialized_) {
        js_cleanup();
    }
}

bool lot_web_device::init() {
    std::cout << "lot_web_device: Starting async init..." << std::endl;
    js_initWebGPU();
    return true;
}

bool lot_web_device::isInitialized() const {
    return js_isInitialized();
}
