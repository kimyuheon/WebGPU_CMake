// DOM 에서 오는 콜백: 레이어 패널 · 노드 트리 · 배열 대화상자 · 속성 패널 · 문자 입력창 · 뷰큐브.
#include "app/lot_app.h"

#include "lot_json.h"
#include "lot_feature.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>
#include <cstdlib>
#include <cstring>

// 레이어 패널에서 온 명령. 실제 처리는 LotLayerPanel 이 한다 (main 은 배선만).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onLayerCommand(const char* action, int layerId, int value) {
    if (action == nullptr) return;
    g_ui.layerPanel().command(action, static_cast<uint32_t>(layerId), value,
                              doc().layers, doc().objects, doc().edit);
}

// 노드 트리: 펼친 층의 객체들을 offset 부터 limit 개. JSON 은 malloc - JS 가 free 한다.
extern "C" EMSCRIPTEN_KEEPALIVE
char* lot_treeChildren(int layerId, int offset, int limit) {
    const std::string j = g_ui.nodeTree().childrenJson(static_cast<uint32_t>(layerId), offset, limit,
                                                       doc().objects, doc().edit);
    char* out = static_cast<char*>(std::malloc(j.size() + 1));
    if (out) std::memcpy(out, j.c_str(), j.size() + 1);
    return out;
}

// 노드 트리에서 온 명령.
//   select   id, value 1 = 기존 선택에 더하기/빼기 (Ctrl/Shift)
//   zoom     id 를 선택하고 그 객체로 줌 (더블 클릭)
//   hide     id 숨기기(value 1) / 보이기(0). id 가 음수면 전부
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onTreeCommand(const char* action, int id, int value) {
    if (action == nullptr) return;
    const std::string a(action);
    const auto oid = static_cast<LotGameObject::id_t>(id);
    if (a == "select" || a == "zoom") {
        const LotGameObject* obj = LotGameObject::find(doc().objects, oid);
        if (!obj) return;
        std::set<LotGameObject::id_t> sel;
        if (a == "select" && value) sel = doc().edit.selection();
        if (a == "select" && value && sel.count(oid)) sel.erase(oid);
        else sel.insert(oid);
        doc().edit.setSelection(std::move(sel));
        LOT_LOG("tree: " << a << " object " << id);
        if (a == "zoom") zoomToObjects({oid});
    } else if (a == "hide") {
        g_ui.nodeTree().setHidden(id < 0 ? LotGameObject::kInvalidId : oid, value != 0,
                                  doc().objects, doc().edit);
    }
}

// 배열 대화상자의 값 (바뀔 때마다). 각은 도.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onArrayParams(int polar, int cols, int rows, float dx, float dy, int count, float angle,
                       int rotateItems, int centerAuto, float cx, float cy, float cz) {
    ArrayTool::Params p = g_array.params();
    p.polar = polar != 0;
    p.cols = cols;
    p.rows = rows;
    p.dx = dx;
    p.dy = dy;
    p.count = count;
    p.angle = angle;
    p.rotateItems = rotateItems != 0;
    p.centerAuto = centerAuto != 0;
    p.center = vec3{cx, cy, cz};
    g_array.setParams(p);
}

// 배열 대화상자 단추: create / close / pickCenter
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onArrayAction(const char* action) {
    if (action == nullptr) return;
    const std::string a(action);
    if (a == "create") {
        g_array.create(doc().objects, doc().edit.history());
    } else if (a == "close") {
        cancelTools();
    } else if (a == "pickCenter") {
        g_array.beginPickCenter();
    }
}

// 속성 패널에서 고친 값 (위치, 문자 내용/높이). 층/색/선종류는 레이어 명령으로 온다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onPropertyEdit(const char* key, const char* value) {
    if (key == nullptr || value == nullptr) return;
    // 솔리드 높이는 형상을 다시 만들어야 해서 (GPU 메시) 여기서 - 네이티브 setBRepParameters
    if (std::string(key) == "solidHeight" && doc().edit.selection().size() == 1) {
        std::string why;
        const float h = static_cast<float>(std::atof(value));
        if (!lot_feature::setHeight(doc().objects, *doc().edit.selection().begin(), h, g_renderer->getDevice(),
                                    doc().edit.history(), why)) {
            LOT_LOG("property: height " << value << " - " << why);
        }
        return;
    }
    g_ui.propertyPanel().edit(key, value, doc().objects, doc().edit);
}

// 문자 입력창에서 Enter. 내용은 JS 가 잡아 준 UTF-8 버퍼 (JS 가 해제한다).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onTextEntered(const char* text) {
    if (text == nullptr) return;
    const std::string content(text);

    // 기존 문자를 고치는 중이면 내용만 바꾼다 (히스토리에 남는다)
    if (g_editingTextId != LotGameObject::kInvalidId) {
        const LotGameObject::id_t id = g_editingTextId;
        g_editingTextId = LotGameObject::kInvalidId;
        auto* obj = LotGameObject::find(doc().objects, id);
        if (!obj || !obj->isText() || obj->text.content == content) return;
        EditHistory::Edit edit;
        edit.label = "text edit";
        edit.before = EditHistory::snapshot(doc().objects, std::set<LotGameObject::id_t>{id});
        if (content.empty()) {
            doc().objects.erase(id);  // 비우면 지운다 (CAD 관례)
            LOT_LOG("text: object " << id << " removed (empty)");
        } else {
            obj->text.content = content;
            edit.after = EditHistory::snapshot(doc().objects, std::set<LotGameObject::id_t>{id});
            LOT_LOG("text: object " << id << " edited (\"" << content << "\")");
        }
        if (edit.after.empty() && !content.empty()) return;
        doc().edit.history().record(std::move(edit));
        return;
    }

    g_sketch.submitText(content, doc().objects);
    if (const auto id = g_sketch.consumeCommittedId(); id != LotGameObject::kInvalidId) {
        if (auto* obj = LotGameObject::find(doc().objects, id)) obj->layer = doc().layers.current();
        doc().edit.history().recordCreated("text", doc().objects, id);
    }
}

// 문자 입력창에서 Esc (또는 포커스 잃음).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onTextCancelled() {
    g_editingTextId = LotGameObject::kInvalidId;
    if (g_sketch.waitingForTextInput()) g_sketch.cancel();
}

// 뷰큐브에서 누른 칸. 방향 성분은 -1/0/1 조합이다 (면 하나 · 모서리 둘 · 꼭짓점 셋).
// 표준 뷰와 똑같이 보이도록 축 하나짜리는 resetCadView 로 넘겨 네이티브와 같은 자세를 쓴다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onViewCube(int dx, int dy, int dz) {
    if (dx == 0 && dy == 0 && dz == 0) return;
    const int axes = (dx != 0) + (dy != 0) + (dz != 0);
    if (axes == 1) {
        LotCamera::CadViewType type = LotCamera::CadViewType::Top;
        if (dz > 0)      type = LotCamera::CadViewType::Top;
        else if (dz < 0) type = LotCamera::CadViewType::Bottom;
        else if (dy > 0) type = LotCamera::CadViewType::Back;
        else if (dy < 0) type = LotCamera::CadViewType::Front;
        else if (dx > 0) type = LotCamera::CadViewType::Right;
        else             type = LotCamera::CadViewType::Left;
        doc().camera.setCadViewDirection(type);   // 보던 자리와 거리는 그대로
        LOT_LOG("viewcube: face (" << dx << ", " << dy << ", " << dz << ")");
        return;
    }
    doc().camera.setViewFromDirection(normalize(vec3(static_cast<float>(dx),
                                                 static_cast<float>(dy),
                                                 static_cast<float>(dz))));
    LOT_LOG("viewcube: " << (axes == 2 ? "edge" : "corner")
            << " (" << dx << ", " << dy << ", " << dz << ")");
}

// 뷰큐브를 잡고 끌기 (픽셀). 캔버스 우클릭 궤도와 같은 감도 · 방향.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onViewCubeDrag(float dx, float dy) {
    doc().camera.orbitAroundTarget(-dx * kOrbitRadPerPixel, -dy * kOrbitRadPerPixel);
}
