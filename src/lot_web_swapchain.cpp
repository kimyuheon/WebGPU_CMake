#include "lot_web_swapchain.h"
#include "lot_web_device.h"
#include "lot_web_common.h"

#include <emscripten/html5.h>
#include <iostream>

// 상태바 높이 (픽셀) - JS 쪽 DOM 구성과 공유하는 유일한 상수
static const int STATUS_BAR_HEIGHT = 150;

// 캔버스 셀렉터 - webgpu_bindings.js 가 만드는 엘리먼트의 id
static const char* CANVAS_SELECTOR = "#webgpu-canvas";

// DOM 구성만 담당하는 JS 헬퍼 (webgpu_bindings.js)
extern "C" {
    extern void js_setupCanvas(int statusBarHeight);
    extern void js_setCanvasSize(int width, int height);
    extern int js_getWindowWidth();
    extern int js_getWindowHeight(int statusBarHeight);
}

// 리사이즈 콜백용 전역 포인터
static lot_web_swapchain* g_swapchainInstance = nullptr;

static EM_BOOL onCanvasResize(int /*eventType*/, const EmscriptenUiEvent* /*uiEvent*/, void* userData) {
    auto* swapchain = static_cast<lot_web_swapchain*>(userData);
    swapchain->resize(js_getWindowWidth(), js_getWindowHeight(STATUS_BAR_HEIGHT));
    return EM_TRUE;
}

lot_web_swapchain::lot_web_swapchain() {
    std::cout << "lot_web_swapchain: Constructor (dynamic size)" << std::endl;
    g_swapchainInstance = this;
}

lot_web_swapchain::~lot_web_swapchain() {
    std::cout << "lot_web_swapchain: Destructor" << std::endl;
    releaseCurrentImage();
    if (surface_) wgpuSurfaceRelease(surface_);
    g_swapchainInstance = nullptr;
}

void lot_web_swapchain::createSwapchain(lot_web_device& device) {
    std::cout << "lot_web_swapchain: Creating surface..." << std::endl;

    // 1. DOM (상태바 + 캔버스) 구성
    js_setupCanvas(STATUS_BAR_HEIGHT);

    width_ = js_getWindowWidth();
    height_ = js_getWindowHeight(STATUS_BAR_HEIGHT);
    js_setCanvasSize(width_, height_);

    // 2. 캔버스를 가리키는 서피스 생성
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvasSource.selector = lotStringView(CANVAS_SELECTOR);

    WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
    surfaceDesc.nextInChain = &canvasSource.chain;
    surfaceDesc.label = lotStringView("Canvas Surface");

    surface_ = wgpuInstanceCreateSurface(device.getInstance(), &surfaceDesc);
    if (!surface_) {
        std::cerr << "lot_web_swapchain: Failed to create surface!" << std::endl;
        return;
    }

    // 3. 선호 포맷 조회 (예전 JS 의 getPreferredCanvasFormat 에 해당)
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(surface_, device.getAdapter(), &caps) == WGPUStatus_Success
        && caps.formatCount > 0) {
        format_ = caps.formats[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    } else {
        std::cerr << "lot_web_swapchain: getCapabilities failed, falling back to BGRA8Unorm"
                  << std::endl;
        format_ = WGPUTextureFormat_BGRA8Unorm;
    }

    device_ = device.getDevice();
    configure();
    configured_ = true;

    // 4. 리사이즈 리스너 등록 (html5.h - JS 리스너 불필요)
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_FALSE, onCanvasResize);

    std::cout << "lot_web_swapchain: Initial size " << width_ << "x" << height_ << std::endl;
    std::cout << "lot_web_swapchain: Format " << static_cast<int>(format_) << std::endl;
}

void lot_web_swapchain::configure() {
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = device_;
    config.format = format_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = static_cast<uint32_t>(width_);
    config.height = static_cast<uint32_t>(height_);
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;

    wgpuSurfaceConfigure(surface_, &config);
}

void lot_web_swapchain::resize(int width, int height) {
    if (width == width_ && height == height_) {
        return;  // 크기 변화 없음
    }
    if (width <= 0 || height <= 0) {
        return;  // 창이 최소화된 경우 등
    }

    width_ = width;
    height_ = height;
    wasResized_ = true;

    if (!configured_) {
        return;
    }

    js_setCanvasSize(width_, height_);
    configure();

    std::cout << "Resized: " << width_ << "x" << height_ << std::endl;
}

WGPUTextureView lot_web_swapchain::acquireNextImage() {
    if (!configured_) {
        return nullptr;
    }

    WGPUSurfaceTexture surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
        && surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        if (surfaceTexture.texture) {
            wgpuTextureRelease(surfaceTexture.texture);
        }
        return nullptr;
    }

    currentTexture_ = surfaceTexture.texture;
    currentView_ = wgpuTextureCreateView(currentTexture_, nullptr);
    return currentView_;
}

void lot_web_swapchain::releaseCurrentImage() {
    if (currentView_) {
        wgpuTextureViewRelease(currentView_);
        currentView_ = nullptr;
    }
    if (currentTexture_) {
        wgpuTextureRelease(currentTexture_);
        currentTexture_ = nullptr;
    }
}
