#include "lot_web_swapchain.h"
#include <emscripten/emscripten.h>
#include <iostream>

// 전역 포인터 (리사이즈 콜백용)
static lot_web_swapchain* g_swapchainInstance = nullptr;

// 상태바 높이 (픽셀)
static const int STATUS_BAR_HEIGHT = 150;

// JavaScript 함수 선언 (webgpu_bindings.js에서 구현)
extern "C" {
    extern void js_createSwapchain(int statusBarHeight);
    extern void js_resizeSwapchain(int width, int height);
    extern int js_getCanvasWidth();
    extern int js_getCanvasHeight(int statusBarHeight);
    extern bool js_isSwapchainReady();
    extern void js_beginRenderPass();
    extern void js_endRenderPass();
    extern void js_setupResizeListener(int statusBarHeight);
}

// 리사이즈 콜백 (JavaScript에서 호출됨)
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void onWindowResize(int width, int height) {
        if (g_swapchainInstance) {
            g_swapchainInstance->resize(width, height);
        }
    }
}

// C++ 구현
lot_web_swapchain::lot_web_swapchain() {
    std::cout << "lot_web_swapchain: Constructor (dynamic size)" << std::endl;
    g_swapchainInstance = this;
}

lot_web_swapchain::~lot_web_swapchain() {
    std::cout << "lot_web_swapchain: Destructor" << std::endl;
    g_swapchainInstance = nullptr;
}

void lot_web_swapchain::createSwapchain() {
    std::cout << "lot_web_swapchain: Creating swapchain..." << std::endl;
    js_createSwapchain(STATUS_BAR_HEIGHT);

    // 초기 크기 저장 (상태바 높이 제외)
    width_ = js_getCanvasWidth();
    height_ = js_getCanvasHeight(STATUS_BAR_HEIGHT);

    // 리사이즈 리스너 등록
    js_setupResizeListener(STATUS_BAR_HEIGHT);

    std::cout << "lot_web_swapchain: Initial size " << width_ << "x" << height_ << std::endl;
}

void lot_web_swapchain::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return;  // 크기 변화 없음
    }

    width_ = width;
    height_ = height;
    wasResized_ = true;

    js_resizeSwapchain(width, height);
}

void lot_web_swapchain::beginRenderPass() {
    if (!isReady()) return;
    js_beginRenderPass();
}

void lot_web_swapchain::endRenderPass() {
    if (!isReady()) return;
    js_endRenderPass();
}

bool lot_web_swapchain::isReady() const {
    return js_isSwapchainReady();
}
