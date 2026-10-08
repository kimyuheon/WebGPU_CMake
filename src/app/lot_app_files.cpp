// 파일: .dxf / .lot / .obj 열기와 .lot / .dxf 저장 (JS 가 바이트를 넘기고 받는다).
#include "app/lot_app.h"

#include "lot_scene_io.h"
#include "lot_layers.h"
#include "lot_dxf.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>
#include <cstdlib>
#include <cstring>

// DXF 메시 (3DFACE · MESH · 폴리페이스) 의 삼각형 -> 모델. 면마다 평평한 노멀 (네이티브 import 의 Mesh 쪽).
std::shared_ptr<LotModel> buildFaceMesh(const std::vector<vec3>& tris) {
    if (tris.size() < 3 || !g_renderer) return nullptr;
    LotModel::Builder b;
    b.vertices.reserve(tris.size());
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const vec3 n0 = cross(tris[i + 1] - tris[i], tris[i + 2] - tris[i]);
        const vec3 n = dot(n0, n0) > 1e-24f ? normalize(n0) : vec3{0.0f, 0.0f, 1.0f};
        for (size_t k = 0; k < 3; ++k) {
            const vec3& q = tris[i + k];
            b.vertices.push_back(Vertex::make(q.x, q.y, q.z, 1.0f, 1.0f, 1.0f, n.x, n.y, n.z, 0.0f, 0.0f));
        }
    }
    return std::make_shared<LotModel>(g_renderer->getDevice(), b);
}

// DXF 열기. JS 가 코드페이지를 풀어 UTF-8 로 넘긴다 (옛 도면은 CP949 등).
// 버퍼는 JS 가 malloc 으로 잡은 것이라 여기서 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onDxfFileLoaded(const char* data, int length, const char* fileName) {
    if (data == nullptr) return;
    const std::string text(data, static_cast<size_t>(length));
    std::free(const_cast<char*>(data));

    // 파싱이 실패하면 지금 씬을 건드리지 않도록 임시 맵에 먼저 읽는다
    LotGameObject::Map loaded;
    LotLayers loadedLayers;
    const lot_dxf::LoadStats stats = lot_dxf::load(text, loaded, loadedLayers);
    if (!stats.error.empty()) {
        LOT_ERR(stats.error);
        return;
    }
    for (const auto& [id, tris] : stats.meshTriangles) {
        if (LotGameObject* o = LotGameObject::find(loaded, id)) o->model = buildFaceMesh(tris);
    }

    openIntoDocument(std::move(loaded), std::move(loadedLayers), fileName);
    doc().dxfOriginX = stats.originX;
    doc().dxfOriginY = stats.originY;
}

// 씬 저장. JS 가 파일로 내려준다. 문자열은 malloc 으로 잡아 넘기고 JS 가 free 한다.
// 내려받기가 곧 저장이라 여기서 '저장됨' 으로 친다 (탭의 점이 지워진다).
// DXF 내보내기는 그렇지 않다 - 메시가 빠지므로 도면을 다 담은 것이 아니다.
extern "C" EMSCRIPTEN_KEEPALIVE
char* lot_saveScene() {
    const std::string text = lot_scene::save(doc().objects, doc().layers);
    if (doc().path.empty()) doc().path = doc().name + ".lot";
    doc().markSaved();
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// 씬을 DXF 로. 2D 스케치/문자/치수만 나간다 (메시는 .lot 으로 저장한다).
extern "C" EMSCRIPTEN_KEEPALIVE
char* lot_saveDxf() {
    const std::string text = lot_dxf::save(doc().objects, doc().layers, doc().linetypeScale, nullptr,
                                           doc().dxfOriginX, doc().dxfOriginY);
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// 씬 열기. 현재 씬을 버리고 파일 것으로 바꾼다 (히스토리/선택도 비운다).
// data 는 JS 가 malloc 으로 잡아 넘긴 버퍼라 여기서 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onLotFileLoaded(const char* data, int length, const char* fileName) {
    if (data == nullptr) return;
    const std::string text(data, static_cast<size_t>(length));
    std::free(const_cast<char*>(data));

    if (!g_renderer || !g_renderer->getSwapchain().isReady()) {
        LOT_ERR("scene: renderer is not ready yet");
        return;
    }

    // 파싱이 실패하면 현재 씬을 건드리지 않도록 임시 맵에 먼저 읽는다
    LotGameObject::Map loaded;
    LotLayers loadedLayers;
    // 파일 메시에는 텍스처를 입히지 않는다 (네이티브와 같다). 체커는 UV 가 맞는지 보려던
    // 시험용이라, mm 도면처럼 UV 가 큰 메시에서는 잔무늬로 뭉개져 면이 안 보인다.
    const lot_scene::LoadStats stats =
        lot_scene::load(text, g_renderer->getDevice(), nullptr, loaded, loadedLayers);
    if (!stats.error.empty()) {
        LOT_ERR(stats.error);
        return;
    }

    openIntoDocument(std::move(loaded), std::move(loadedLayers), fileName);
}

// data 는 JS 가 malloc 으로 잡아 넘긴 버퍼다. 해제는 여기 책임이다.
// 길이를 같이 받는 이유는 널 종료가 아니기 때문이다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onObjFileLoaded(const char* data, int length) {
    if (data == nullptr) return;

    const std::string text(data, static_cast<size_t>(length));
    std::free(const_cast<char*>(data));

    if (!g_renderer || !g_renderer->getSwapchain().isReady()) {
        LOT_ERR("OBJ open: renderer is not ready yet");
        return;
    }

    auto model = LotModel::createFromObjText(g_renderer->getDevice(), text, "(opened file)");
    if (!model) {
        return;  // 실패 이유는 파서/모델 쪽에서 이미 출력했다
    }

    // 이전 모델은 shared_ptr 이 마지막으로 놓을 때 정리된다
    g_objModel = std::move(model);

    if (auto* object = LotGameObject::find(doc().objects, doc().objObjectId)) {
        object->model = g_objModel;
        object->transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
        LOT_LOG("OBJ open: replaced the model at scene center");
    } else if (doc().objPlaced) {
        // 자리에 있던 오브젝트가 지워졌다 - 새로 놓는다
        placeObjModel();
    }
    // 아직 자리를 못 잡았으면 렌더 루프의 4-1 이 넣어준다
}
