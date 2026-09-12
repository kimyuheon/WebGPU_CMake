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

    // false 면 뎁스 비교를 Always 로 두고 쓰지도 않는다 - 항상 위에 그려진다.
    // 기즈모처럼 가려지면 안 되는 것에 쓴다. 렌더 패스에 뎁스 어태치먼트가
    // 있는 한 뎁스 상태 자체는 있어야 하므로 '끄는' 게 아니라 '통과시키는' 것이다.
    bool depthTest = true;

    // 알파 블렌딩 (src alpha, 1 - src alpha). 반투명한 것 - 기즈모 평면 핸들,
    // 고스트 미리보기 - 에 쓴다. 반투명은 불투명한 것을 다 그린 뒤에 그려야
    // 하고, 뎁스를 쓰지 않는 편이 안전하다 (뒤의 반투명이 앞의 것에 가려지므로).
    bool alphaBlend = false;
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
