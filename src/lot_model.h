#pragma once

#include "lot_web_buffer.h"
#include "lot_vertex.h"

#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>
#include <vector>

class lot_web_device;

// 정점/인덱스 버퍼를 소유하고 그리기까지 담당하는 모델.
//
// 예전에는 게임 오브젝트가 lot_web_buffer* 와 vertexCount 를 따로 들고 있었는데,
// 그러면 "정점이 몇 개인지"를 오브젝트마다 다시 적어줘야 했다.
// 이제 그 정보는 모델 안에만 있다.
class LotModel {
public:
    // 정점 데이터를 모아서 넘기는 용도.
    // indices 가 비어 있으면 인덱스 없이 draw 한다.
    struct Builder {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    LotModel(lot_web_device& device, const Builder& builder);
    ~LotModel() = default;

    // 복사 금지
    LotModel(const LotModel&) = delete;
    LotModel& operator=(const LotModel&) = delete;

    void bind(WGPURenderPassEncoder pass);
    void draw(WGPURenderPassEncoder pass);

    bool isReady() const;

    // 정육면체 (한 변 1.0, 중심이 원점)
    static std::unique_ptr<LotModel> createCube(lot_web_device& device);

private:
    std::unique_ptr<lot_web_buffer> vertexBuffer_;
    std::unique_ptr<lot_web_buffer> indexBuffer_;

    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
};
