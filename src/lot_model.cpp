#include "lot_model.h"
#include "lot_math.h"
#include "lot_web_device.h"

#include <iostream>

LotModel::LotModel(lot_web_device& device, const Builder& builder) {
    vertexCount_ = static_cast<uint32_t>(builder.vertices.size());
    if (vertexCount_ < 3) {
        std::cerr << "LotModel: need at least 3 vertices!" << std::endl;
        return;
    }

    vertexBuffer_ = std::make_unique<lot_web_buffer>(
        BufferType::VERTEX, sizeof(Vertex) * builder.vertices.size());
    vertexBuffer_->createBuffer(device, builder.vertices.data());

    indexCount_ = static_cast<uint32_t>(builder.indices.size());
    if (indexCount_ > 0) {
        indexBuffer_ = std::make_unique<lot_web_buffer>(
            BufferType::INDEX, sizeof(uint32_t) * builder.indices.size());
        indexBuffer_->createBuffer(device, builder.indices.data());
    }

    std::cout << "LotModel: " << vertexCount_ << " vertices, "
              << indexCount_ << " indices" << std::endl;
}

bool LotModel::isReady() const {
    if (!vertexBuffer_ || !vertexBuffer_->isReady()) return false;
    if (indexCount_ > 0 && (!indexBuffer_ || !indexBuffer_->isReady())) return false;
    return true;
}

void LotModel::bind(WGPURenderPassEncoder pass) {
    if (!isReady() || pass == nullptr) return;

    vertexBuffer_->bind(pass, 0);
    if (indexBuffer_) {
        indexBuffer_->bind(pass);
    }
}

void LotModel::draw(WGPURenderPassEncoder pass) {
    if (!isReady() || pass == nullptr) return;

    if (indexCount_ > 0) {
        wgpuRenderPassEncoderDrawIndexed(pass, indexCount_, 1, 0, 0, 0);
    } else {
        wgpuRenderPassEncoderDraw(pass, vertexCount_, 1, 0, 0);
    }
}

std::unique_ptr<LotModel> LotModel::createCube(lot_web_device& device) {
    // 좌표 규약: +X 오른쪽, +Y 아래, +Z 화면 안쪽.
    // 그래서 '윗면'은 y = -0.5 이다.
    //
    // 각 면의 네 꼭짓점은 바깥쪽 법선이 나오는 순서로 적는다
    // (오른손 법칙: cross(b-a, c-a) 가 면의 바깥을 향한다).
    // 이 순서가 여섯 면 모두 일관되어야 백페이스 컬링을 켤 수 있다.
    const vec3 kFaceColors[6] = {
        {0.9f, 0.9f, 0.9f},  // 왼쪽  - 흰색
        {0.8f, 0.8f, 0.1f},  // 오른쪽 - 노랑
        {0.9f, 0.6f, 0.1f},  // 위    - 주황
        {0.8f, 0.1f, 0.1f},  // 아래  - 빨강
        {0.1f, 0.1f, 0.8f},  // 앞    - 파랑
        {0.1f, 0.8f, 0.1f},  // 뒤    - 초록
    };

    const float kFaceCorners[6][4][3] = {
        // 왼쪽 (x = -0.5, 법선 -X)
        {{-0.5f, -0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}},
        // 오른쪽 (x = +0.5, 법선 +X)
        {{ 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f}},
        // 위 (y = -0.5, 법선 -Y)
        {{-0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f,  0.5f}},
        // 아래 (y = +0.5, 법선 +Y)
        {{-0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f}},
        // 앞 (z = -0.5, 법선 -Z, 카메라 쪽)
        {{-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f}},
        // 뒤 (z = +0.5, 법선 +Z)
        {{ 0.5f, -0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f}},
    };

    Builder builder;
    builder.vertices.reserve(24);
    builder.indices.reserve(36);

    for (uint32_t face = 0; face < 6; ++face) {
        const vec3& color = kFaceColors[face];
        for (int corner = 0; corner < 4; ++corner) {
            const float* p = kFaceCorners[face][corner];
            builder.vertices.push_back(Vertex{{p[0], p[1], p[2]},
                                              {color.x, color.y, color.z}});
        }

        // 사각형 하나를 삼각형 둘로: (a, b, c) 와 (a, c, d)
        const uint32_t base = face * 4;
        builder.indices.push_back(base + 0);
        builder.indices.push_back(base + 1);
        builder.indices.push_back(base + 2);
        builder.indices.push_back(base + 0);
        builder.indices.push_back(base + 2);
        builder.indices.push_back(base + 3);
    }

    return std::make_unique<LotModel>(device, builder);
}
