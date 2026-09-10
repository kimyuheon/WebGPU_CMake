#include "lot_web_device.h"
#include "lot_web_common.h"
#include "lot_log.h"

lot_web_device::lot_web_device() {
    LOT_LOG("lot_web_device: Constructor");
}

lot_web_device::~lot_web_device() {
    LOT_LOG("lot_web_device: Destructor");
    if (queue_) wgpuQueueRelease(queue_);
    if (device_) wgpuDeviceRelease(device_);
    if (adapter_) wgpuAdapterRelease(adapter_);
    if (instance_) wgpuInstanceRelease(instance_);
}

bool lot_web_device::init() {
    LOT_LOG("lot_web_device: Starting async init...");

    instance_ = wgpuCreateInstance(nullptr);
    if (!instance_) {
        LOT_ERR("lot_web_device: Failed to create instance!");
        return false;
    }

    requestAdapter();
    return true;
}

void lot_web_device::requestAdapter() {
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo callbackInfo = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    // AllowSpontaneous: JS 프로미스가 풀리는 즉시 콜백이 불린다.
    // (별도의 ProcessEvents 펌프가 필요 없다 - 기존 폴링 루프를 그대로 쓸 수 있다)
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = onAdapterReady;
    callbackInfo.userdata1 = this;

    wgpuInstanceRequestAdapter(instance_, &options, callbackInfo);
}

void lot_web_device::onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                    WGPUStringView message, void* userdata1, void* /*userdata2*/) {
    auto* self = static_cast<lot_web_device*>(userdata1);

    if (status != WGPURequestAdapterStatus_Success || adapter == nullptr) {
        LOT_ERR("lot_web_device: requestAdapter failed: "
              << lotToString(message));
        return;
    }

    self->adapter_ = adapter;
    self->logAdapterInfo();
    self->requestDevice();
}

void lot_web_device::logAdapterInfo() {
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter_, &info) != WGPUStatus_Success) {
        LOT_LOG("lot_web_device: adapter info unavailable");
        return;
    }

    LOT_LOG("========================================");
    LOT_LOG("GPU Information:");
    LOT_LOG("  Vendor: " << lotToString(info.vendor));
    LOT_LOG("  Architecture: " << lotToString(info.architecture));
    LOT_LOG("  Device: " << lotToString(info.device));
    LOT_LOG("  Description: " << lotToString(info.description));

    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(adapter_, &limits) == WGPUStatus_Success) {
        LOT_LOG("GPU Limits:");
        LOT_LOG("  Max Texture Size: " << limits.maxTextureDimension2D);
        LOT_LOG("  Max Buffer Size: "
              << (limits.maxBufferSize / 1024 / 1024) << " MB");
    }
    LOT_LOG("========================================");

    wgpuAdapterInfoFreeMembers(info);
}

void lot_web_device::requestDevice() {
    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.label = lotStringView("Lot Web Device");

    // 예전 JS 바인딩에는 없던 부분: GPU 에러가 조용히 사라지지 않도록 잡는다.
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    desc.deviceLostCallbackInfo.callback = onDeviceLost;
    desc.deviceLostCallbackInfo.userdata1 = this;

    desc.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    desc.uncapturedErrorCallbackInfo.userdata1 = this;

    WGPURequestDeviceCallbackInfo callbackInfo = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = onDeviceReady;
    callbackInfo.userdata1 = this;

    wgpuAdapterRequestDevice(adapter_, &desc, callbackInfo);
}

void lot_web_device::onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device,
                                   WGPUStringView message, void* userdata1, void* /*userdata2*/) {
    auto* self = static_cast<lot_web_device*>(userdata1);

    if (status != WGPURequestDeviceStatus_Success || device == nullptr) {
        LOT_ERR("lot_web_device: requestDevice failed: "
              << lotToString(message));
        return;
    }

    self->device_ = device;
    self->queue_ = wgpuDeviceGetQueue(device);
    self->initialized_ = true;

    LOT_LOG("lot_web_device: Initialized successfully!");
}

void lot_web_device::onDeviceLost(WGPUDevice const* /*device*/, WGPUDeviceLostReason reason,
                                  WGPUStringView message, void* /*userdata1*/, void* /*userdata2*/) {
    LOT_ERR("lot_web_device: DEVICE LOST (reason " << static_cast<int>(reason) << "): "
              << lotToString(message));
}

void lot_web_device::onUncapturedError(WGPUDevice const* /*device*/, WGPUErrorType type,
                                       WGPUStringView message, void* /*userdata1*/, void* /*userdata2*/) {
    LOT_ERR("lot_web_device: UNCAPTURED ERROR (type " << static_cast<int>(type) << "): "
              << lotToString(message));
}
