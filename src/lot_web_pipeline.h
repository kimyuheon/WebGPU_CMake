#pragma once

#include <webgpu/webgpu.h>
#include <string>

class lot_web_device;

// 렌더 시스템마다 다르게 주는 파이프라인 설정.
// 메시는 삼각형 + 백페이스 컬링, 그리드/선은 선분 + 컬링 없음 식이다.
// Vulkan 쪽 PipelineConfigInfo 에 해당하지만 지금 필요한 것만 담았다.
struct PipelineConfig {
    WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
    WGPUCullMode cullMode = WGPUCullMode_Back;
};

class lot_web_pipeline {
public:
    lot_web_pipeline(const std::string& shaderPath);
    ~lot_web_pipeline();

    // 복사 금지
    lot_web_pipeline(const lot_web_pipeline&) = delete;
    lot_web_pipeline& operator=(const lot_web_pipeline&) = delete;

    // 파이프라인 생성 (셰이더 파일을 비동기로 받아온 뒤 완성된다)
    //
    // layout 은 렌더 시스템이 만들어 넘긴다 (Vulkan 쪽 pipelineLayout 과 같은 역할).
    // nullptr 을 주면 'auto' 레이아웃이 되는데, auto 로는 dynamic offset 을
    // 켤 수 없으므로 오브젝트별 uniform 을 쓰려면 반드시 명시해야 한다.
    //
    // depthFormat 이 Undefined 면 뎁스 테스트 없이 만든다.
    void createPipeline(lot_web_device& device, WGPUTextureFormat colorFormat,
                        WGPUTextureFormat depthFormat, WGPUPipelineLayout layout,
                        const PipelineConfig& config = PipelineConfig{});

    // 파이프라인 바인딩.
    // 실제 draw 는 정점 개수를 아는 LotModel 이 한다.
    void bind(WGPURenderPassEncoder pass);

    bool isReady() const { return pipeline_ != nullptr; }
    WGPURenderPipeline getHandle() const { return pipeline_; }

private:
    // 셰이더 로드 콜백 (emscripten_async_wget_data)
    static void onShaderLoaded(void* arg, void* buffer, int size);
    static void onShaderFailed(void* arg);

    void build(const std::string& shaderCode);

    std::string shaderPath_;
    WGPUDevice device_ = nullptr;
    WGPUTextureFormat colorFormat_ = WGPUTextureFormat_Undefined;
    WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Undefined;
    WGPUPipelineLayout layout_ = nullptr;
    PipelineConfig config_{};
    WGPURenderPipeline pipeline_ = nullptr;
};
