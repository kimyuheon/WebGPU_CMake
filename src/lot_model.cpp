#include "lot_model.h"
#include "lot_math.h"
#include "lot_obj_loader.h"
#include "lot_web_device.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

LotModel::LotModel(lot_web_device& device, const Builder& builder) {
    vertexCount_ = static_cast<uint32_t>(builder.vertices.size());
    if (vertexCount_ < 3) {
        LOT_ERR("LotModel: need at least 3 vertices!");
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

    // CPU 사본. 정점 전체는 씬 저장용, 위치 배열은 피킹/스냅이 빠르게 돌게 따로.
    vertices_ = builder.vertices;
    positions_.reserve(builder.vertices.size());
    for (const auto& v : builder.vertices) {
        positions_.push_back(vec3{v.position[0], v.position[1], v.position[2]});
    }
    indices_ = builder.indices;

    // 경계 상자
    boundsMin_ = vec3{builder.vertices[0].position[0],
                      builder.vertices[0].position[1],
                      builder.vertices[0].position[2]};
    boundsMax_ = boundsMin_;
    for (const auto& v : builder.vertices) {
        boundsMin_.x = std::fmin(boundsMin_.x, v.position[0]);
        boundsMin_.y = std::fmin(boundsMin_.y, v.position[1]);
        boundsMin_.z = std::fmin(boundsMin_.z, v.position[2]);
        boundsMax_.x = std::fmax(boundsMax_.x, v.position[0]);
        boundsMax_.y = std::fmax(boundsMax_.y, v.position[1]);
        boundsMax_.z = std::fmax(boundsMax_.z, v.position[2]);
    }

    buildFeatureEdges(35.0f);   // 네이티브가 메시에 쓰는 값

    LOT_LOG("LotModel: " << vertexCount_ << " vertices, "
              << indexCount_ << " indices, " << featureEdges_.size() / 2 << " feature edges");
}

// 네이티브 LotModel::Builder::buildFeatureEdges 와 같은 규칙.
// 1) 위치를 1e-5 단위로 반올림해 같은 자리 정점을 하나로 본다 (면마다 정점을 따로 둔
//    큐브도 모서리를 공유하게). 2) 삼각형 변마다 붙은 면의 법선을 모은다.
// 3) 면이 하나뿐이면 가장자리, 멀쩡한 면 둘이면 법선 사이 각이 임계보다 클 때 모서리,
//    셋 이상이면 비다양체라 일단 그린다.
// 바늘/퇴화 삼각형(높이 < 가장 긴 변의 0.1%)은 법선이 제멋대로라 개수로만 센다 -
// 꺾임 판정에 넣으면 평평한 벽 한가운데에 선이 그어진다.
void LotModel::buildFeatureEdges(float angleThresholdDeg) {
    featureEdges_.clear();
    const size_t triCount = getTriangleCount();
    if (triCount == 0) return;

    using Key = std::tuple<long long, long long, long long>;
    auto quantize = [](const vec3& p) {
        return Key{std::llround(p.x * 100000.0), std::llround(p.y * 100000.0),
                   std::llround(p.z * 100000.0)};
    };
    std::map<Key, uint32_t> welded;   // 자리 -> 대표 번호
    auto weld = [&](const vec3& p) {
        const uint32_t next = static_cast<uint32_t>(welded.size());
        return welded.emplace(quantize(p), next).first->second;
    };

    struct EdgeInfo {
        vec3 a, b;
        vec3 normal0, normal1;
        int faces = 0;    // 붙은 삼각형 전부
        int strong = 0;   // 그중 바늘/퇴화가 아닌 것 - normal0/1 은 이것들의 법선
    };
    std::map<std::pair<uint32_t, uint32_t>, EdgeInfo> edges;

    for (size_t t = 0; t < triCount; ++t) {
        vec3 p[3];
        if (!getTriangle(t, p[0], p[1], p[2])) continue;
        const vec3 e1 = p[1] - p[0], e2 = p[2] - p[0], e3 = p[2] - p[1];
        const vec3 n = cross(e1, e2);
        const float len = std::sqrt(dot(n, n));
        const float longest2 = std::fmax(dot(e1, e1), std::fmax(dot(e2, e2), dot(e3, e3)));
        const bool weak = len < 1e-8f || len < 1e-3f * longest2;
        const vec3 normal = (len > 0.0f) ? n * (1.0f / len) : vec3{0.0f, 0.0f, 1.0f};
        const uint32_t w[3] = {weld(p[0]), weld(p[1]), weld(p[2])};
        for (int e = 0; e < 3; ++e) {
            const int f = (e + 1) % 3;
            if (w[e] == w[f]) continue;
            const auto key = std::minmax(w[e], w[f]);
            EdgeInfo& info = edges[{key.first, key.second}];
            if (info.faces == 0) { info.a = p[e]; info.b = p[f]; }
            ++info.faces;
            if (weak) continue;
            if (info.strong == 0) info.normal0 = normal;
            else if (info.strong == 1) info.normal1 = normal;
            ++info.strong;
        }
    }

    const float cosThreshold = std::cos(angleThresholdDeg * 3.14159265f / 180.0f);
    for (const auto& entry : edges) {
        const EdgeInfo& info = entry.second;
        bool feature = false;
        if (info.faces == 1) feature = true;                   // 가장자리
        else if (info.strong < 2) continue;                    // 바늘 삼각형 사이
        else if (info.strong == 2) feature = dot(info.normal0, info.normal1) < cosThreshold;
        else feature = true;                                   // 비다양체
        if (!feature) continue;
        featureEdges_.push_back(info.a);
        featureEdges_.push_back(info.b);
    }
}

size_t LotModel::getTriangleCount() const {
    return (indices_.empty() ? positions_.size() : indices_.size()) / 3;
}

bool LotModel::getTriangle(size_t i, vec3& a, vec3& b, vec3& c) const {
    const size_t base = i * 3;
    if (indices_.empty()) {
        if (base + 2 >= positions_.size()) return false;
        a = positions_[base];
        b = positions_[base + 1];
        c = positions_[base + 2];
        return true;
    }
    if (base + 2 >= indices_.size()) return false;
    const uint32_t ia = indices_[base], ib = indices_[base + 1], ic = indices_[base + 2];
    if (ia >= positions_.size() || ib >= positions_.size() || ic >= positions_.size()) return false;
    a = positions_[ia];
    b = positions_[ib];
    c = positions_[ic];
    return true;
}

vec3 LotModel::boundsCenter() const {
    return (boundsMin_ + boundsMax_) * 0.5f;
}

float LotModel::fitScale(float targetSize) const {
    const vec3 extent = boundsMax_ - boundsMin_;
    const float largest = std::fmax(extent.x, std::fmax(extent.y, extent.z));
    if (largest <= 0.0f) return 1.0f;  // 점 하나짜리 모델 - 나눗셈을 피한다
    return targetSize / largest;
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
    // 정육면체는 대칭이라 축 규약과 무관하다. 아래의 '위/아래/앞/뒤' 이름은
    // 면을 구별하는 표지일 뿐이다 (월드는 Z-up: +Z 위, +Y 앞).
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

    // 면마다 바깥을 향하는 법선. 위의 감는 방향과 반드시 일치해야 한다.
    const vec3 kFaceNormals[6] = {
        {-1.0f,  0.0f,  0.0f},  // 왼쪽
        { 1.0f,  0.0f,  0.0f},  // 오른쪽
        { 0.0f, -1.0f,  0.0f},  // -Y 면
        { 0.0f,  1.0f,  0.0f},  // 아래
        { 0.0f,  0.0f, -1.0f},  // 앞 (카메라 쪽)
        { 0.0f,  0.0f,  1.0f},  // 뒤
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
        const vec3& normal = kFaceNormals[face];
        // 꼭짓점 순서가 (a, b, c, d) 로 한 바퀴 도는 사각형이므로
        // UV 도 (0,0) (0,1) (1,1) (1,0) 으로 한 바퀴 돌리면 면마다 텍스처 한 장이다.
        const float kFaceUV[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
        for (int corner = 0; corner < 4; ++corner) {
            const float* p = kFaceCorners[face][corner];
            // 면의 네 꼭짓점이 같은 법선을 쓴다 - 그래서 면이 평평하게 보인다
            // (부드럽게 하려면 꼭짓점을 공유하고 법선을 평균내야 한다).
            builder.vertices.push_back(Vertex::make(p[0], p[1], p[2],
                                                    color.x, color.y, color.z,
                                                    normal.x, normal.y, normal.z,
                                                    kFaceUV[corner][0], kFaceUV[corner][1]));
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

namespace {

// 비동기 fetch 콜백은 C 함수 포인터라 캡처를 못 넘긴다.
// 그래서 필요한 것들을 힙에 담아 void* 로 들려 보낸다.
struct ObjLoadContext {
    lot_web_device* device;
    std::string path;
    std::function<void(std::unique_ptr<LotModel>)> onLoaded;
};

void onObjLoaded(void* arg, void* buffer, int size) {
    // 콜백이 어떻게 끝나든 컨텍스트는 여기서 정리된다
    std::unique_ptr<ObjLoadContext> ctx{static_cast<ObjLoadContext*>(arg)};

    // wget_data 의 버퍼는 널 종료가 아니므로 길이를 명시해 복사한다
    const std::string text(static_cast<const char*>(buffer), static_cast<size_t>(size));

    ctx->onLoaded(LotModel::createFromObjText(*ctx->device, text, ctx->path));
}

void onObjFailed(void* arg) {
    std::unique_ptr<ObjLoadContext> ctx{static_cast<ObjLoadContext*>(arg)};
    LOT_ERR("LotModel: failed to fetch " << ctx->path);
    ctx->onLoaded(nullptr);
}

}  // namespace

std::unique_ptr<LotModel> LotModel::createFromObjText(lot_web_device& device,
                                                      const std::string& text,
                                                      const std::string& label) {
    lot_obj::LoadResult parsed = lot_obj::parse(text);
    if (!parsed.ok) {
        LOT_ERR("LotModel: failed to parse " << label
              << " - " << parsed.error);
        return nullptr;
    }

    LOT_LOG("LotModel: loaded " << label);
    auto model = std::make_unique<LotModel>(device, parsed.builder);
    if (!model->isReady()) {
        return nullptr;
    }
    return model;
}

void LotModel::loadFromObjAsync(lot_web_device& device, const std::string& path,
                                std::function<void(std::unique_ptr<LotModel>)> onLoaded) {
    auto* ctx = new ObjLoadContext{&device, path, std::move(onLoaded)};
    LOT_LOG("LotModel: fetching " << path);
    emscripten_async_wget_data(path.c_str(), ctx, onObjLoaded, onObjFailed);
}
