#pragma once

#include "lot_web_buffer.h"
#include "lot_math.h"
#include "lot_vertex.h"

#include <webgpu/webgpu.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
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

    // 경계 상자 (모델 로컬 공간). 남이 만든 OBJ 는 크기가 제각각이라
    // (몇 백 단위짜리도 흔하다) 화면에 맞추려면 이게 필요하고,
    // 피킹도 이 상자로 한다.
    const vec3& boundsMin() const { return boundsMin_; }
    const vec3& boundsMax() const { return boundsMax_; }

    // CPU 쪽 지오메트리 (모델 로컬 공간). 삼각형 단위 피킹과 스냅이 쓴다.
    // GPU 에 올린 것과 별개로 위치만 따로 들고 있다 - 메모리는 정점당 12 바이트.
    const std::vector<vec3>& getPositions() const { return positions_; }
    const std::vector<uint32_t>& getIndices() const { return indices_; }

    // 삼각형 개수. 인덱스가 없으면 정점 셋씩 이어진 것으로 본다.
    // (선분 모델에는 의미 없다 - 피킹은 게임 오브젝트의 메시에만 한다.)
    size_t getTriangleCount() const;

    // 삼각형 i 의 세 꼭짓점 (로컬). i 가 범위 밖이면 false.
    bool getTriangle(size_t i, vec3& a, vec3& b, vec3& c) const;
    vec3 boundsCenter() const;

    // 가장 긴 변이 targetSize 가 되도록 하는 스케일.
    // 지오메트리를 건드리지 않고 transform 으로만 맞춘다.
    float fitScale(float targetSize) const;

    // 정육면체 (한 변 1.0, 중심이 원점)
    static std::unique_ptr<LotModel> createCube(lot_web_device& device);

    // OBJ 파일 '내용'으로 모델을 만든다. 실패하면 nullptr.
    // 파일에서 읽어오든 사용자가 고른 파일이든 결국 여기로 모인다.
    static std::unique_ptr<LotModel> createFromObjText(lot_web_device& device,
                                                       const std::string& text,
                                                       const std::string& label);

    // OBJ 파일을 받아와서 모델을 만든다.
    //
    // 웹에는 동기 파일 읽기가 없다. 셰이더와 마찬가지로 fetch 로 받아오므로
    // 결과는 콜백으로 온다. 실패하면 nullptr 이 넘어온다.
    //
    // device 는 콜백이 불릴 때까지 살아 있어야 한다.
    static void loadFromObjAsync(lot_web_device& device, const std::string& path,
                                 std::function<void(std::unique_ptr<LotModel>)> onLoaded);

private:
    std::unique_ptr<lot_web_buffer> vertexBuffer_;
    std::unique_ptr<lot_web_buffer> indexBuffer_;

    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;

    vec3 boundsMin_{0.0f, 0.0f, 0.0f};
    vec3 boundsMax_{0.0f, 0.0f, 0.0f};

    std::vector<vec3> positions_;
    std::vector<uint32_t> indices_;
};
