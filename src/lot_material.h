#pragma once

#include "lot_texture.h"

#include <webgpu/webgpu.h>
#include <memory>

class lot_web_device;

// 재질 = 셰이더의 @group(2). 지금은 텍스처 + 샘플러뿐이다.
//
// 색/거칠기 같은 값이 들어오면 여기에 유니폼 버퍼가 하나 붙는다.
// 메시의 알파(반투명 재질)도 그때 이 유니폼으로 간다.
//
// 텍스처가 없는 오브젝트는 SimpleRenderSystem 의 기본 재질(1x1 흰색)을 쓴다.
// 셰이더가 분기 없이 항상 곱하도록 하는 편이 파이프라인을 하나로 유지한다.
class LotMaterial {
public:
    // @group(2) 레이아웃. 렌더 시스템이 한 번 만들어 파이프라인 레이아웃에 넣고,
    // 재질들은 같은 레이아웃으로 자기 바인드 그룹을 만든다.
    static WGPUBindGroupLayout createBindGroupLayout(lot_web_device& device);

    LotMaterial(lot_web_device& device, WGPUBindGroupLayout layout,
                std::shared_ptr<LotTexture> texture);
    ~LotMaterial();

    LotMaterial(const LotMaterial&) = delete;
    LotMaterial& operator=(const LotMaterial&) = delete;

    WGPUBindGroup getBindGroup() const { return bindGroup_; }
    bool isReady() const { return bindGroup_ != nullptr; }

    const std::shared_ptr<LotTexture>& getTexture() const { return texture_; }

private:
    // 바인드 그룹이 뷰/샘플러를 참조하므로 텍스처가 재질보다 오래 살아야 한다
    std::shared_ptr<LotTexture> texture_;
    WGPUBindGroup bindGroup_ = nullptr;
};
