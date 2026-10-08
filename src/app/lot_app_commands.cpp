// 명령: '#이름' (메뉴 · 리본 · 명령행이 같은 코드를 보낸다), 명령행 글자, 툴바 키.
#include "app/lot_app.h"

#include "lot_osnap.h"
#include "lot_edit_ops.h"
#include "lot_feature.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>
#include <cstdlib>

// 명령행에서 친 글자. 이름이면 그 명령을, 숫자면 열린 도구의 값 입력으로 보낸다.
// 문자열은 JS 가 잡아 준 것이라 JS 가 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onCommandLine(const char* typed) {
    if (typed == nullptr) return;
    const std::string text(typed);
    if (text.empty()) return;

    // 도구의 값 · 옵션 (숫자, 끊기의 @, 길이조정의 de 0.5 ...)
    if (typedToolValue(text)) return;

    const lot_ui::LotCommandLine::Resolved r = g_ui.commandLine().resolve(text);
    if (!r.found) {
        LOT_LOG("command: unknown \"" << text << "\"");
        return;
    }
    LOT_LOG("command: " << text << " -> " << r.label);
    if (runAction(r.keyCode.c_str())) return;
    g_cameraController.handleBrowserKey(r.keyCode.c_str(), true, r.ctrl);
    g_cameraController.handleBrowserKey(r.keyCode.c_str(), false, r.ctrl);
}

// 보고 있는 자리에 큐브 하나. 기본 씬의 큐브와 같은 모델을 쓴다 (정점은 GPU 에
// 한 번만 올라가 있다). 자리는 카메라가 보는 점의 XY, 높이는 바닥에 딱 얹히게.
// 색은 네이티브 addNewCube 처럼 아무 색이나 하나 - 여러 개를 놓아도 구별된다.
void addCube() {
    if (!g_cubeModel) {
        LOT_ERR("cube: the cube model is not ready yet");
        return;
    }
    const vec3 look = doc().camera.getTarget();
    auto cube = LotGameObject::createGameObject();
    cube.model = g_cubeModel;
    cube.transform.translation = vec3(look.x, look.y, kCubeHalf);
    cube.transform.scale = vec3(0.6f);
    cube.color = vec3((std::rand() % 100) / 100.0f, (std::rand() % 100) / 100.0f,
                      (std::rand() % 100) / 100.0f);
    cube.colorByLayer = false;
    cube.layer = doc().layers.current();
    const auto id = cube.getId();
    doc().objects.emplace(id, std::move(cube));
    doc().edit.history().recordCreated("cube", doc().objects, id);
    doc().edit.setSelection({id});   // 바로 기즈모로 옮길 수 있게
    LOT_LOG("cube: added object " << id << " at (" << look.x << ", " << look.y << ")");
}

// 관통 컷 (네이티브 requestCutThroughSelected): 돌출 솔리드 하나 + 닫힌 스케치들. 스케치마다 한 번의 실행 취소.
void cutThroughSelected() {
    LotGameObject::id_t solid = LotGameObject::kInvalidId;
    std::vector<LotGameObject::id_t> sketches;
    int solids = 0;
    for (const auto id : doc().edit.selection()) {
        const LotGameObject* o = LotGameObject::find(doc().objects, id);
        if (!o) continue;
        if (o->isSolid()) { solid = id; ++solids; }
        else if (o->isSketch() && o->closed) sketches.push_back(id);
    }
    if (solids != 1 || sketches.empty()) {
        LOT_LOG("cutthrough: select one extruded solid and closed sketch(es) first");
        return;
    }
    int done = 0;
    for (const auto sk : sketches) {
        std::string why;
        if (lot_feature::cut(doc().objects, solid, sk, 0.0f, true, g_renderer->getDevice(), doc().edit.history(), why)) ++done;
        else LOT_LOG("cutthrough: sketch " << sk << " - " << why);
    }
    if (done > 0) doc().edit.setSelection({solid});
}

// 분해 / 결합을 지금 선택에
void runPendingOp(const std::string& op) {
    if (op == "explode") {
        const auto made = lot_edit_ops::explode(doc().objects, doc().edit.selection(), doc().edit.history());
        if (!made.empty()) doc().edit.clearSelection();
    } else if (op == "join") {
        std::string why;
        const auto id = lot_edit_ops::join(doc().objects, doc().edit.selection(), doc().edit.history(), why);
        if (id == LotGameObject::kInvalidId) LOT_LOG("join: " << why);
        else doc().edit.setSelection({id});
    }
}

// 키가 아닌 명령 ('#이름'). 메뉴/리본/명령행이 같은 코드를 보낸다.
//
// 그릴 거리가 늘어날수록 알파벳이 모자란다 - 큐브·구·원기둥에 글쇠를 하나씩
// 떼어 주면 금세 바닥난다. 단축키가 필요 없는 명령은 이 길로 보낸다.
// true 를 돌려주면 처리한 것.
bool runAction(const char* code) {
    if (code == nullptr || code[0] != '#') return false;
    const std::string name(code + 1);
    // 3D 피처 명령
    if (name == "extrude" || name == "boss" || name == "pocket") {
        cancelTools();
        g_extrude.start(name == "extrude" ? ExtrudeTool::Mode::Extrude
                        : name == "boss"  ? ExtrudeTool::Mode::Boss : ExtrudeTool::Mode::Pocket,
                        doc().edit.selection(), doc().objects);
        if (g_extrude.isActive() && g_extrude.wantsNumber()) doc().edit.clearSelection();
        return true;
    }
    if (name == "cutthrough") {
        cancelTools();
        cutThroughSelected();
        return true;
    }
    if (name != "dims" && name.rfind("style:", 0) != 0 && name.rfind("osnap", 0) != 0) g_extrude.cancel();
    if (name == "cube") { addCube(); return true; }
    if (name == "eraseAll") { doc().edit.deleteAll(doc().objects); return true; }
    if (name == "newDoc") { newDocument(); return true; }
    if (name == "closeDoc") { closeDocument(g_documentIndex); return true; }
    if (name == "nextDoc") { stepDocument(1); return true; }
    if (name == "prevDoc") { stepDocument(-1); return true; }
    // 분해 / 결합: 선택이 있으면 바로, 없으면 고르고 Enter
    if (name == "explode" || name == "join") {
        cancelTools();
        if (doc().edit.selection().empty()) {
            g_pendingOp = name;
            LOT_LOG(name << ": select objects, then Enter (Esc cancels)");
        } else {
            runPendingOp(name);
        }
        return true;
    }
    if (name == "stretch" || name == "lengthen") {
        cancelTools();
        doc().edit.clearSelection();
        if (name == "stretch") g_stretch.start(doc().camera);
        else g_lengthen.start(doc().camera);
        return true;
    }
    if (name == "break") {
        cancelTools();
        doc().edit.clearSelection();
        g_break.start(doc().camera);
        return true;
    }
    if (name == "array") {
        cancelTools();
        g_array.start(doc().edit.selection(), doc().camera, doc().objects);
        return true;
    }
    if (name == "fillet" || name == "chamfer") {
        cancelTools();
        doc().edit.clearSelection();
        // 기본값은 도면 크기에 맞춘 그리드 간격 (한 번 정하면 그 값을 기억한다)
        g_fillet.start(name == "fillet" ? FilletTool::Mode::Fillet : FilletTool::Mode::Chamfer, doc().camera,
                       doc().gridSpacing);
        return true;
    }
    if (name == "trim" || name == "extend") {
        cancelTools();
        doc().edit.clearSelection();
        g_trim.start(name == "trim" ? TrimTool::Mode::Trim : TrimTool::Mode::Extend, doc().camera);
        return true;
    }
    if (name == "offset") {
        cancelTools();
        // 기본 거리는 도면 크기에 맞춘 그리드 간격 (한 번 쳤으면 그 값을 기억한다)
        g_offset.start(doc().camera, doc().gridSpacing);
        return true;
    }
    if (name == "mirror") {
        cancelTools();
        g_transform.start(TransformTool::Mode::Mirror, doc().edit.selection(), doc().camera, doc().objects);
        return true;
    }
    if (name == "dims") {
        g_display.dims = !g_display.dims;
        LOT_LOG("display: dimensions " << (g_display.dims ? "on" : "off"));
        return true;
    }
    // 비주얼 스타일 '#style:N'
    if (name.rfind("style:", 0) == 0) {
        const int st = std::atoi(name.c_str() + 6);
        if (st >= 0 && st < DisplaySettings::StyleCount) {
            g_display.style = st;
            LOT_LOG("display: style " << kStyleNames[st]);
        }
        return true;
    }
    // 객체스냅 종류 '#osnapKind:N' (켜고 끄기), 전체 켜기/끄기
    if (name.rfind("osnapKind:", 0) == 0) {
        const auto kind = static_cast<lot_osnap::Kind>(std::atoi(name.c_str() + 10));
        if (kind > lot_osnap::Kind::None && kind < lot_osnap::Kind::Count) {
            lot_osnap::enabledKinds() ^= lot_osnap::kindBit(kind);
            LOT_LOG("osnap: " << lot_osnap::kindName(kind) << (lot_osnap::isEnabled(kind) ? " on" : " off"));
        }
        return true;
    }
    if (name == "osnapAll" || name == "osnapNone") {
        unsigned all = 0;
        for (int k = 1; k < static_cast<int>(lot_osnap::Kind::Count); ++k) all |= lot_osnap::kindBit(static_cast<lot_osnap::Kind>(k));
        lot_osnap::enabledKinds() = (name == "osnapAll") ? all : 0u;
        LOT_LOG("osnap: all kinds " << (name == "osnapAll" ? "on" : "off"));
        return true;
    }
    LOT_ERR("command: no action named \"" << name << "\"");
    return true;   // '#' 로 왔으면 키로 넘기지 않는다
}

// 툴바 버튼. 키보드 이벤트와 같은 경로를 타게 눌렀다 뗀 것으로 넣는다 -
// 버튼과 단축키가 어긋날 수 없다. code 는 JS 가 잡은 버퍼라 JS 가 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onToolbarKey(const char* code, int ctrl) {
    if (code == nullptr) return;
    if (runAction(code)) return;
    g_cameraController.handleBrowserKey(code, true, ctrl != 0);
    g_cameraController.handleBrowserKey(code, false, ctrl != 0);
}
