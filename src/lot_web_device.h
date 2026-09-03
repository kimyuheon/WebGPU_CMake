#pragma once

#include <webgpu/webgpu.h>

class lot_web_device {
public:
    lot_web_device();
    ~lot_web_device();

    // 복사 금지
    lot_web_device(const lot_web_device&) = delete;
    lot_web_device& operator=(const lot_web_device&) = delete;

    // 초기화 (비동기 - adapter -> device 순으로 콜백이 이어진다)
    bool init();

    // 초기화 완료 확인
    bool isInitialized() const { return initialized_; }

    WGPUInstance getInstance() const { return instance_; }
    WGPUAdapter getAdapter() const { return adapter_; }
    WGPUDevice getDevice() const { return device_; }
    WGPUQueue getQueue() const { return queue_; }

private:
    void requestAdapter();
    void requestDevice();
    void logAdapterInfo();

    // 비동기 콜백 (userdata1 로 this 를 넘긴다)
    static void onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                               WGPUStringView message, void* userdata1, void* userdata2);
    static void onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device,
                              WGPUStringView message, void* userdata1, void* userdata2);
    static void onDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                             WGPUStringView message, void* userdata1, void* userdata2);
    static void onUncapturedError(WGPUDevice const* device, WGPUErrorType type,
                                  WGPUStringView message, void* userdata1, void* userdata2);

    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    bool initialized_ = false;
};
