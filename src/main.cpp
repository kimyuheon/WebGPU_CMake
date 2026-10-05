#include "lot_web_renderer.h"
#include "simple_render_system.h"
#include "lot_grid.h"
#include "line_render_system.h"
#include "polyline_render_system.h"
#include "gizmo_render_system.h"
#include "post_process_system.h"
#include "lot_frame_info.h"
#include "lot_render_target.h"
#include "lot_edit_controller.h"
#include "lot_scene_io.h"
#include "lot_dimension.h"
#include "lot_osnap.h"
#include "lot_json.h"
#include "lot_layers.h"
#include "lot_cursor_snap.h"
#include "lot_document.h"
#include "lot_dxf.h"
#include "ui/lot_ui.h"
#include "lot_cursor_snap.h"
#include "lot_linetype.h"
#include "lot_sketch_tool.h"
#include "lot_offset_tool.h"
#include "lot_transform_tool.h"
#include "text_render_system.h"
#include "lot_mouse_input.h"
#include "lot_global_uniform.h"
#include "lot_game_object.h"
#include "lot_camera.h"
#include "lot_keyboard_controller.h"
#include "lot_lighting.h"
#include "lot_model.h"
#include "lot_material.h"
#include "lot_texture.h"
#include "lot_math.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" {
    // OBJ 로더(Module.lotDom.objLoad)를 등록한다. 파일 선택은 통합 열기 대화상자(lot_panels.js).
    extern void js_setupObjFileInput();
    // 툴바 (src/js/lot_toolbar.js). 버튼은 단축키 코드를 lot_onToolbarKey 로 돌려보낸다.
    extern void js_setupPanels();
    // 문자 도구 입력창 (lot_toolbar.js). Enter 는 lot_onTextEntered, Esc 는 lot_onTextCancelled 로 온다.
    extern void js_showTextInput(const char* placeholder, const char* initial);
    extern void js_hideTextInput();
    // 선종류 목록 (한 번). 레이어 패널의 드롭다운을 채운다.
    extern void js_uiSetLinetypes(const char* json);
    // 툴바가 아직 없으면 0 을 돌려준다 - 그러면 다음 프레임에 다시 보낸다.
}

// 전역 객체들
std::unique_ptr<LotWebRenderer> g_renderer = nullptr;
std::unique_ptr<SimpleRenderSystem> g_renderSystem = nullptr;
std::unique_ptr<LineRenderSystem> g_lineSystem = nullptr;
std::unique_ptr<PolylineRenderSystem> g_polylineSystem = nullptr;
std::unique_ptr<GizmoRenderSystem> g_gizmoSystem = nullptr;

// 장면은 먼저 오프스크린 타깃에 그려지고, 후처리 패스가 그걸 화면에 옮긴다.
// 두 패스 구조라서 색/뎁스를 다음 패스가 읽을 수 있다 (외곽선, 그림자, GPU 피킹).
LotRenderTarget g_sceneTarget;
std::unique_ptr<PostProcessSystem> g_postSystem = nullptr;
std::unique_ptr<TextRenderSystem> g_textSystem = nullptr;

// 카메라 + 조명 유니폼. 렌더 시스템 전부가 이 하나를 @group(0) 으로 본다.
LotGlobalUniform g_globalUniform;
std::shared_ptr<LotModel> g_cubeModel = nullptr;
std::shared_ptr<LotModel> g_objModel = nullptr;

// 시험용 체커보드 재질. 텍스처 파이프라인과 UV 가 맞는지 눈으로 보는 용도.
// 큐브와 OBJ 가 같은 재질을 공유한다 (shared_ptr).
std::shared_ptr<LotMaterial> g_checkerMaterial = nullptr;
KeyboardMovementController g_cameraController;
MouseInput g_mouse;
SketchController g_sketch;
lot_ui::LotUi g_ui;
TransformTool g_transform;
OffsetTool g_offset;

// 열려 있는 도면들과 지금 보고 있는 것. 탭 하나가 도면 하나다.
// 도면에만 속하는 것(오브젝트·층·히스토리·시점)은 전부 LotDocument 안에 있어,
// doc() 가 가리키는 곳만 바꾸면 화면이 통째로 그 도면 것이 된다.
std::vector<std::unique_ptr<LotDocument>> g_documents;
size_t g_documentIndex = 0;

LotDocument& doc() { return *g_documents[g_documentIndex]; }

// 화면 표시 토글 (하단 상태바). 도면이 아니라 보는 사람의 설정이라 탭을 바꿔도 그대로다.
struct DisplaySettings {
    bool grid = true;      // F7 바닥 격자
    bool dims = true;      // 치수 표시 (끄면 고를 수도 없다)
    // 비주얼 스타일 (상태바의 셰이딩 메뉴)
    enum Style { Shaded, ShadedEdges, WireMesh, WireEdges, HiddenLine, StyleCount };
    int style = ShadedEdges;
    bool fillFaces() const { return style == Shaded || style == ShadedEdges || style == HiddenLine; }
    bool featureEdges() const { return style == ShadedEdges || style == WireEdges || style == HiddenLine; }
};
static const char* kStyleNames[] = {"shaded", "shaded + edges", "wireframe (mesh)", "wireframe (edges)", "hidden line"};
static DisplaySettings g_display;

// 애니메이션 시간
static double g_time = 0.0;
static double g_lastFrameMs = 0.0;

// 초기화 상태
static bool g_modelCreated = false;
static bool g_objRequested = false;   // fetch 를 시작했는지 (한 번만 보낸다)

// 불러온 모델이 화면에 차는 크기. 남이 만든 OBJ 는 단위가 제각각이라
// (몇 백 단위짜리도 흔하다) 파일 값을 그대로 쓰면 안 보이거나 화면을 덮는다.
static const float kObjTargetSize = 1.4f;
static const float kObjHeight = 0.6f;       // 가운데 모델의 중심 높이 (바닥 위)
static const float kCubeHalf = 0.3f;        // 큐브 반 변 (scale 0.6) - 바닥에 딱 얹히게
static const vec3 kMeshEdgeColor{0.08f, 0.08f, 0.08f};  // 메시 모서리 선 (거의 검정)
static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_overlayCreated = false;
static bool g_gameObjectsCreated = false;

// 카메라 설정
static const float kFovY = 50.0f * 3.14159265f / 180.0f;
// 클립 평면은 씬 크기에 따라 움직인다 (zoomExtents 가 정한다). 기본값은 미터 단위
// 장난감 씬용이고, mm 도면을 열면 수천 배로 늘어난다. near 를 같이 키워야 깊이
// 정밀도가 남는다 (near 0.1 에 far 100000 이면 z-fighting 이 심하다).
static const vec3 kCameraStartPosition{0.0f, -2.5f, 0.6f};  // (FPS 모드) 앞(-Y)에서 눈높이로

// CAD 궤도 조작 감도
static const float kOrbitRadPerPixel = 0.005f;  // 우클릭 드래그 1px 당 (한 바퀴 ≈ 1250px)
static const float kOrbitRadPerSec = 1.5f;      // 화살표로 돌릴 때

// 투영 모드. P 키로 전환한다.
// 직교의 halfHeight 는 화면 세로 절반에 담기는 월드 길이 - 곧 줌이다.
// F8 직교 / F9 그리드 스냅. 실제 값은 lot_cursor::settings() 한 벌 - 도구들이 그걸 읽는다.
// 그리드 간격은 씬 크기에 맞춰 정한다 (zoomExtents).
// 전역 선종류 축척 (AutoCAD 의 LTSCALE). 씬을 열 때 크기에 맞춰 잡는다.
static const float kOrthoZoomSpeed = 2.0f;      // 초당 배율

// 조명. 광원은 큐브들 위쪽 앞에 두고 천천히 돌린다.
// 감쇠가 거리 제곱에 반비례하므로 세기는 거리의 제곱 규모로 잡아야 한다
// (거리 1.5 면 감쇠가 1/2.25 이라, 세기 4 정도는 되어야 눈에 찬다).
static SceneLighting g_lighting = [] {
    SceneLighting lighting;
    lighting.ambientIntensity = 0.15f;   // 네이티브의 최솟값 (평행광이 있어 0.03 이면 그늘이 새까맣다)
    lighting.pointLight.color = vec3(1.0f, 1.0f, 1.0f);
    lighting.pointLight.intensity = 4.0f;
    return lighting;
}();

// 광원은 큐브들 '앞쪽'(카메라 쪽, -Z)에서 좌우로 오간다.
// 큐브와 같은 깊이(z = 0)에 두면 카메라를 향한 앞면이 전부 광원을 등지게 되어
// 화면이 통째로 어두워진다 - 물리적으로는 맞지만 볼 게 없다.
static const float kLightSwingX = 1.5f;
static const float kLightHeight = 1.4f;   // Z-up: 바닥 위 1.4
static const float kLightBaseY = -1.2f;   // 카메라 쪽(-Y)으로 조금
static const float kLightSwingY = 0.8f;
static const float kLightOrbitSpeed = 0.8f;

// 게임 오브젝트 생성
void createGameObjects() {
    // 큐브 하나를 여러 오브젝트가 공유한다 (모델이 shared_ptr 인 이유).
    // 정점 데이터는 GPU 에 한 번만 올라가고, 오브젝트마다 다른 것은 transform 뿐이다.
    // 가운데는 OBJ 로 불러온 모델 자리로 비워둔다.
    const vec3 spawns[] = {
        vec3(-1.5f, 0.0f, kCubeHalf),
        vec3( 1.5f, 0.0f, kCubeHalf),
    };

    for (const auto& translation : spawns) {
        auto cube = LotGameObject::createGameObject();

        // 재질 없이 면마다 다른 정점 색 (네이티브 기본 씬의 큐브와 같다).
        // 체커 텍스처는 텍스처 길을 보여 주는 토러스에만 남긴다.
        cube.model = g_cubeModel;
        cube.transform.translation = translation;
        cube.transform.scale = vec3(0.6f);
        // 세 면이 다 보이게 위 축(Z) 둘레로 살짝 돌린다 (바닥에 얹힌 채로)
        cube.transform.rotation = quat::angleAxis(0.6f, vec3(0.0f, 0.0f, 1.0f));

        const auto id = cube.getId();
        doc().objects.emplace(id, std::move(cube));
    }

    // 광원. 전에는 프레임마다 십자를 그려 넣어서 고를 수도 지울 수도 없었고
    // '전체 지우기' 뒤에도 남아 있었다. 평범한 오브젝트로 둔다.
    //
    // 같이 그리던 궤도선·나선·작업영역 상자는 없앴다. 폴리라인 파이프라인이
    // 도는지 눈으로 보려던 시연용인데, 이제 스케치마다 그 길을 지나므로
    // 도면에 남을 이유가 없다 (지울 수도 없는 선이 도면에 섞이면 더 나쁘다).
    {
        auto light = LotGameObject::createGameObject();
        light.transform.translation = vec3(kLightSwingX, kLightBaseY, kLightHeight);
        light.color = vec3(1.0f, 0.95f, 0.6f);
        light.light.valid = true;
        light.light.color = vec3(1.0f, 1.0f, 1.0f);
        light.light.intensity = 4.0f;
        light.light.orbit = true;   // 옮기면 그 자리에 선다
        doc().objects.emplace(light.getId(), std::move(light));
    }
    LOT_LOG("Game objects created: " << doc().objects.size());
}

// 받아온 OBJ 모델을 장면 가운데에 놓는다.
//
// fetch 콜백은 렌더 루프 바깥(브라우저 이벤트 루프)에서 불리므로,
// 프레임 도중에 벡터가 바뀔 걱정은 없다.
void placeObjModel() {
    auto object = LotGameObject::createGameObject();
    object.model = g_objModel;
    object.material = g_checkerMaterial;
    object.transform.translation = vec3(0.0f, 0.0f, kObjHeight);
    // OBJ 는 Y-up 관례라 Z-up 세계에서는 X 둘레 +90도 돌려 세운다. 토러스는 그러면
    // 구멍이 위를 보고 눕는다 - 살짝 기울여 구멍이 보이게.
    object.transform.rotation = normalize(quat::angleAxis(0.3f, vec3(0.0f, 0.0f, 1.0f))
                                          * quat::angleAxis(1.2f, vec3(1.0f, 0.0f, 0.0f)));
    object.transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
    doc().objObjectId = object.getId();
    doc().objects.emplace(doc().objObjectId, std::move(object));

    doc().objPlaced = true;
    LOT_LOG("OBJ model placed at scene center");
}

// 사용자가 고른 OBJ 파일이 도착했을 때 JS 가 부른다.
//
// 선종류 목록을 패널에 한 번. 표준 8종 + Continuous.
static void pushLinetypes() {
    std::string j = "[";
    bool first = true;
    for (const lot_linetype::Definition& d : lot_linetype::standard()) {
        if (!first) j += ",";
        first = false;
        j += "{\"id\":" + std::to_string(d.id)
           + ",\"name\":" + lot_ui::quote(d.name)
           + ",\"sample\":" + lot_ui::quote(d.sample) + "}";
    }
    j += "]";
    js_uiSetLinetypes(j.c_str());
}

// 문자는 문자열마다 비트맵을 구워 텍스처로 올린다. 큰 도면은 글자가 수천~수만 개라
// 다 구우면 멈추고 GPU 메모리도 모자란다. 화면 밖이거나 너무 작아 읽을 수 없는 것은
// 건너뛴다 - 줌인해서 보일 때 그때 굽는다 (한 번 구운 것은 캐시에 남는다).
static bool textWorthDrawing(const LotGameObject& obj, float width, float height) {
    constexpr float kMinPx = 2.0f;   // 이보다 작으면 읽을 수 없다
    const vec3& at = obj.transform.translation;
    const float wpp = doc().camera.worldPerPixel(at, height);
    if (!(wpp > 0.0f)) return true;
    const float px = obj.text.height / wpp;
    if (px < kMinPx) return false;
    float sx = 0.0f, sy = 0.0f;
    if (!doc().camera.projectToScreen(at, width, height, sx, sy)) return false;
    // 글자 폭은 모르니 넉넉히 (UTF-8 바이트 수 x 높이 - 한글은 3 바이트라 더 넉넉하다)
    const float reach = px * (2.0f + static_cast<float>(obj.text.content.size()));
    return sx > -reach && sx < width + reach && sy > -reach && sy < height + reach;
}

// 층 표를 보는 콜백들. 렌더/피킹 시스템은 층을 모르고 이 함수만 부른다.
static bool layerVisible(const LotGameObject& obj) { return !obj.hidden && doc().layers.isVisible(obj.layer); }
static bool layerSelectable(const LotGameObject& obj) {
    if (obj.hidden) return false;                              // 노드 트리에서 숨긴 것
    if (!g_display.dims && obj.isDimension()) return false;  // 안 보이는 치수는 안 잡힌다
    return doc().layers.isSelectable(obj.layer);
}

// 오브젝트가 쓸 선종류. '층 따름'이면 층의 것.
static uint32_t displayLinetype(const LotGameObject& obj) {
    if (obj.linetype != lot_linetype::kByLayer) return obj.linetype;
    const LotLayers::Layer* l = doc().layers.find(obj.layer);
    return l ? l->linetype : lot_linetype::kContinuous;
}

// 오브젝트가 화면에 낼 색. '층 따름'이면 층 색.
static vec3 displayColor(const LotGameObject& obj) {
    if (!obj.colorByLayer) return obj.color;
    const LotLayers::Layer* l = doc().layers.find(obj.layer);
    return l ? l->color : obj.color;
}

static bool runAction(const char* code);   // 아래, 큐브 같은 '키 아닌 명령'

// 명령행에서 친 글자. 이름이면 그 명령을, 숫자면 열린 도구의 값 입력으로 보낸다.
// 문자열은 JS 가 잡아 준 것이라 JS 가 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onCommandLine(const char* typed) {
    if (typed == nullptr) return;
    const std::string text(typed);
    if (text.empty()) return;

    // 숫자(또는 부호/소수점)로 시작하면 값이다 - 진행 중인 변환 도구가 받는다.
    const char first = text[0];
    if (first == '-' || first == '.' || (first >= '0' && first <= '9')) {
        if (g_offset.wantsNumber()) {
            g_offset.setNumberBuffer(text);
            g_offset.finish();
            LOT_LOG("command: value " << text);
        } else if (g_transform.isPreviewing()) {
            g_transform.setNumberBuffer(text);
            const auto& sc = g_renderer->getSwapchain();
            TransformTool::Context tctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(),
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_transform.finish(tctx, doc().edit.history());
            LOT_LOG("command: value " << text);
        } else {
            LOT_LOG("command: " << text << " - no tool is waiting for a value");
        }
        return;
    }

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

// 레이어 패널에서 온 명령. 실제 처리는 LotLayerPanel 이 한다 (main 은 배선만).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onLayerCommand(const char* action, int layerId, int value) {
    if (action == nullptr) return;
    g_ui.layerPanel().command(action, static_cast<uint32_t>(layerId), value,
                              doc().layers, doc().objects, doc().edit);
}

static void zoomToObjects(const std::set<LotGameObject::id_t>& ids);   // 아래 (전체 보기 옆)

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

// 속성 패널에서 고친 값 (위치, 문자 내용/높이). 층/색/선종류는 레이어 명령으로 온다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onPropertyEdit(const char* key, const char* value) {
    if (key == nullptr || value == nullptr) return;
    g_ui.propertyPanel().edit(key, value, doc().objects, doc().edit);
}

// 더블 클릭으로 연 문자 오브젝트 (없으면 kInvalidId). 입력창이 이 오브젝트를 고친다.
static LotGameObject::id_t g_editingTextId = LotGameObject::kInvalidId;

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

// 보고 있는 자리에 큐브 하나. 기본 씬의 큐브와 같은 모델을 쓴다 (정점은 GPU 에
// 한 번만 올라가 있다). 자리는 카메라가 보는 점의 XY, 높이는 바닥에 딱 얹히게.
// 색은 네이티브 addNewCube 처럼 아무 색이나 하나 - 여러 개를 놓아도 구별된다.
static void addCube() {
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

// ── 도면 탭 ─────────────────────────────────────────────────────
// 탭 하나가 LotDocument 하나다. 바꾸는 일은 doc() 가 가리키는 곳을 옮기는 것뿐이고,
// 그 전에 손에 든 도구를 내려놓는다 - 도구는 지금 도면의 오브젝트를 물고 있다.
// 오브젝트 id 는 전역 카운터라 도면끼리 겹치지 않고, 모델/재질은 shared_ptr 라
// 안 보이는 도면의 것도 그대로 살아 있다 (다시 올릴 것이 없다).

static int g_untitledCount = 0;   // '도면N' 의 N

static std::unique_ptr<LotDocument> makeDocument() {
    auto d = std::make_unique<LotDocument>();
    d->name = "도면" + std::to_string(++g_untitledCount);
    // 카메라: CAD 궤도가 기본. 앞-왼쪽-위에서 내려다보는 3/4 뷰로 시작한다.
    // 1인칭(V) 용 뷰어 오브젝트 위치도 같이 잡아둔다.
    d->camera.setViewFromDirection(normalize(vec3{-0.45f, -1.0f, 0.45f}));
    d->viewer.transform.translation = kCameraStartPosition;
    d->objPlaced = true;   // 가운데 토러스는 첫 도면에만 (main 이 풀어 준다)
    return d;
}

// 씬 크기에서 나온 값 중 도면 밖(도구 · 커서 설정)에 들어가 있는 것들을 지금 도면 것으로.
static void applyDocumentScale() {
    g_sketch.setDimensionStyle(doc().dimTextHeight, doc().dimArrowSize);
    g_sketch.setTextHeight(doc().textHeight);
    if (lot_cursor::settings().gridSpacing > 0.0f) lot_cursor::settings().gridSpacing = doc().gridSpacing;
}

static void putDownTools() {
    g_sketch.cancel();
    g_transform.cancel(doc().objects);
    g_offset.cancel();
    g_editingTextId = LotGameObject::kInvalidId;
    js_hideTextInput();
}

static void activateDocument(size_t index) {
    if (index >= g_documents.size() || index == g_documentIndex) return;
    putDownTools();
    g_documentIndex = index;
    applyDocumentScale();
    LOT_LOG("document: switched to \"" << doc().name << "\" (" << index + 1 << "/"
            << g_documents.size() << ")");
}

static void newDocument() {
    putDownTools();
    g_documents.push_back(makeDocument());
    g_documentIndex = g_documents.size() - 1;
    applyDocumentScale();
    LOT_LOG("document: new \"" << doc().name << "\" (" << g_documents.size() << " open)");
}

// 마지막 하나는 닫지 않고 새 빈 도면으로 바꾼다 - 탭은 늘 하나 이상이다 (네이티브와 같다).
static void closeDocument(size_t index) {
    if (index >= g_documents.size()) return;
    if (index == g_documentIndex) putDownTools();
    const std::string name = g_documents[index]->name;
    if (g_documents.size() == 1) {
        g_documents[0] = makeDocument();
    } else {
        g_documents.erase(g_documents.begin() + static_cast<std::ptrdiff_t>(index));
        // 앞의 것이 빠졌거나 맨 끝 것을 닫았으면 한 칸 당긴다
        if (index < g_documentIndex || g_documentIndex >= g_documents.size()) --g_documentIndex;
    }
    applyDocumentScale();
    LOT_LOG("document: closed \"" << name << "\" (" << g_documents.size() << " open, showing \""
            << doc().name << "\")");
}

static void stepDocument(int delta) {
    const int n = static_cast<int>(g_documents.size());
    activateDocument(static_cast<size_t>((static_cast<int>(g_documentIndex) + delta + n) % n));
}

// 탭 줄에서 온 것. index 는 탭 순서 (0 부터).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onDocumentTab(const char* action, int index) {
    if (action == nullptr || index < 0) return;
    const std::string a(action);
    if (a == "activate") activateDocument(static_cast<size_t>(index));
    else if (a == "close") closeDocument(static_cast<size_t>(index));
}

// 키가 아닌 명령 ('#이름'). 메뉴/리본/명령행이 같은 코드를 보낸다.
//
// 그릴 거리가 늘어날수록 알파벳이 모자란다 - 큐브·구·원기둥에 글쇠를 하나씩
// 떼어 주면 금세 바닥난다. 단축키가 필요 없는 명령은 이 길로 보낸다.
// true 를 돌려주면 처리한 것.
static bool runAction(const char* code) {
    if (code == nullptr || code[0] != '#') return false;
    const std::string name(code + 1);
    if (name == "cube") { addCube(); return true; }
    if (name == "eraseAll") { doc().edit.deleteAll(doc().objects); return true; }
    if (name == "newDoc") { newDocument(); return true; }
    if (name == "closeDoc") { closeDocument(g_documentIndex); return true; }
    if (name == "nextDoc") { stepDocument(1); return true; }
    if (name == "prevDoc") { stepDocument(-1); return true; }
    if (name == "offset") {
        g_sketch.cancel();
        g_transform.cancel(doc().objects);
        // 기본 거리는 도면 크기에 맞춘 그리드 간격 (한 번 쳤으면 그 값을 기억한다)
        g_offset.start(doc().camera, doc().gridSpacing);
        return true;
    }
    if (name == "mirror") {
        g_sketch.cancel();
        g_transform.cancel(doc().objects);
        g_offset.cancel();
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

// 툴바 버튼. 키보드 이벤트와 같은 경로를 타게 눌렀다 뗀 것으로 넣는다 -
// 버튼과 단축키가 어긋날 수 없다. code 는 JS 가 잡은 버퍼라 JS 가 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onToolbarKey(const char* code, int ctrl) {
    if (code == nullptr) return;
    if (runAction(code)) return;
    g_cameraController.handleBrowserKey(code, true, ctrl != 0);
    g_cameraController.handleBrowserKey(code, false, ctrl != 0);
}

// 전체 보기 (Zoom Extents). 모든 오브젝트의 월드 경계를 구해 카메라를 맞춘다.
// 클립 평면과 직교 줌 한계도 그 크기에 맞춘다 - 씬 단위가 m 든 mm 든 보이게.
// 객체들의 월드 경계. only 가 있으면 그 객체들만. 하나도 없으면 false.
static bool objectBounds(const std::set<LotGameObject::id_t>* only, vec3& lo, vec3& hi) {
    bool any = false;
    auto grow = [&](const vec3& p) {
        if (!any) { lo = hi = p; any = true; return; }
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    };
    for (const auto& entry : doc().objects) {
        if (only && !only->count(entry.first)) continue;
        const LotGameObject& obj = entry.second;
        if (obj.isSketch()) {
            for (const vec3& p : obj.worldPoints()) grow(p);
        } else if (obj.isDimension()) {
            for (const vec3& p : lot_dim::outlinePoints(obj)) grow(p);
        } else if (obj.isText()) {
            vec3 c[4];
            if (lot_text::quadCorners(obj, c)) for (int i = 0; i < 4; ++i) grow(c[i]);
        } else if (obj.isLight()) {
            grow(obj.transform.translation);
        } else if (obj.model) {
            // 경계 상자 여덟 꼭짓점을 변환한다 (회전한 상자도 안전하게 덮인다)
            const mat4 m = obj.transform.mat4Transform();
            const vec3& a = obj.model->boundsMin();
            const vec3& b = obj.model->boundsMax();
            for (int i = 0; i < 8; ++i) {
                grow(transformPoint(m, vec3{(i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y,
                                            (i & 4) ? b.z : a.z}));
            }
        }
    }
    return any;
}

static void zoomExtents() {
    vec3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f};
    if (!objectBounds(nullptr, lo, hi)) return;

    const vec3 center = (lo + hi) * 0.5f;
    const vec3 half = (hi - lo) * 0.5f;
    const float radius = std::fmax(std::sqrt(dot(half, half)), 0.01f);

    doc().camera.setViewMode(LotCamera::ViewMode::Cad);
    doc().camera.frame(center, radius, kFovY);

    // 직교: 세로 절반에 반지름이 담기게. 한계도 씬 크기에 비례.
    doc().orthoHalfHeight = radius * 1.15f;
    doc().orthoMaxHalfHeight = std::fmax(20.0f, radius * 20.0f);
    doc().orthoMinHalfHeight = std::fmin(0.2f, radius * 0.001f);

    // 클립 평면: far 는 최대 궤도 거리 + 씬 반지름을 덮고, near 는 far 의 1e-5 이상.
    doc().farZ = std::fmax(100.0f, doc().camera.getMaxOrbitDistance() + radius * 2.0f);
    doc().nearZ = std::fmax(0.01f, doc().farZ * 1e-5f);

    // 치수 글자/화살표 기본 크기를 씬에 맞춘다 (mm 도면에서 0.3 짜리 글자는 안 보인다)
    doc().dimTextHeight = radius * 0.05f;
    doc().dimArrowSize = radius * 0.025f;
    doc().textHeight = radius * 0.07f;
    // 선종류 무늬도 씬 크기에 맞춘다 (mm 도면에서 0.5 짜리 파선은 실선처럼 보인다)
    doc().linetypeScale = std::fmax(0.01f, radius * 0.25f);
    // 그리드 스냅 간격도 - 화면 가로에 눈금이 20 개쯤 되게 '보기 좋은' 값으로
    doc().gridSpacing = lot_cursor::niceSpacing(radius * 0.1f);
    applyDocumentScale();

    LOT_LOG("view: zoom extents - center (" << center.x << ", " << center.y << ", " << center.z
            << ") radius " << radius << ", clip " << doc().nearZ << " .. " << doc().farZ);
}

// 객체들로 줌 (노드 트리 더블 클릭). 카메라만 옮긴다 - 클립 평면 · 치수 크기 같은
// 도면 단위 설정은 전체 보기가 정한 그대로 둔다.
static void zoomToObjects(const std::set<LotGameObject::id_t>& ids) {
    vec3 lo, hi;
    if (!objectBounds(&ids, lo, hi)) return;
    const vec3 center = (lo + hi) * 0.5f;
    const vec3 half = (hi - lo) * 0.5f;
    const float radius = std::fmax(std::sqrt(dot(half, half)), doc().orthoMinHalfHeight);
    doc().camera.setViewMode(LotCamera::ViewMode::Cad);
    doc().camera.focus(center, radius, kFovY);
    doc().orthoHalfHeight = std::fmin(std::fmax(radius * 1.15f, doc().orthoMinHalfHeight), doc().orthoMaxHalfHeight);
    LOT_LOG("view: zoom to " << ids.size() << " objects - radius " << radius);
}

// 읽어 들인 파일을 탭에. 지금 탭이 손대지 않은 새 도면이면 그 자리에, 아니면 새 탭에.
// fileName 은 탭 이름과 저장할 때의 파일 이름이 된다 (테스트 도구는 안 넘긴다 - 그러면 그대로).
static void openIntoDocument(LotGameObject::Map&& objects, LotLayers&& layers, const char* fileName) {
    if (doc().pristine()) putDownTools();
    else newDocument();
    doc().edit.clearSelection();
    doc().edit.history().clear();
    doc().objects = std::move(objects);
    doc().layers = std::move(layers);
    doc().objPlaced = true;  // 파일 도면에는 기본 토러스를 끼워 넣지 않는다
    if (fileName != nullptr && fileName[0] != '\0') {
        doc().path = fileName;
        const size_t dot = doc().path.find_last_of('.');
        doc().name = (dot == std::string::npos || dot == 0) ? doc().path : doc().path.substr(0, dot);
    }
    doc().markSaved();
    zoomExtents();       // 단위가 다른 파일(mm 도면)도 바로 보이게
    LOT_LOG("document: \"" << doc().name << "\" opened (" << g_documents.size() << " open)");
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

// 렌더 루프
void renderLoop() {
    if (!g_renderer || !g_renderSystem) return;

    auto& device = g_renderer->getDevice();

    // 1. 모델 생성 (스왑체인 준비 후)
    if (!g_modelCreated && g_renderer->getSwapchain().isReady()) {
        g_cubeModel = LotModel::createCube(device);
        g_modelCreated = g_cubeModel && g_cubeModel->isReady();

        // 캔버스는 스왑체인이 만들므로 이제야 셀렉터로 찾을 수 있다
        g_mouse.init();
        // 툴바도 상태바 높이를 DOM 에서 읽으므로 같은 시점에
        js_setupPanels();
        pushLinetypes();
    }

    // 1-1. OBJ 모델은 네트워크로 받아오므로 요청만 보내두고 넘어간다.
    //      도착하면 콜백에서 g_objModel 이 채워지고, 아래 4-1 에서 장면에 들어간다.
    if (!g_objRequested && g_modelCreated) {
        g_objRequested = true;
        LotModel::loadFromObjAsync(device, "models/torus.obj",
                                   [](std::unique_ptr<LotModel> model) {
                                       if (model) g_objModel = std::move(model);
                                   });
    }

    // 2. Uniform 리소스 생성 (모델 준비 후).
    //    파이프라인보다 먼저다 - 여기서 나온 바인드 그룹 레이아웃으로 파이프라인을 만든다.
    if (!g_uniformCreated && g_modelCreated) {
        // 글로벌(카메라/조명)이 먼저다 - 렌더 시스템들이 이 레이아웃을 받아
        // 자기 파이프라인 레이아웃의 slot 0 으로 쓴다.
        g_globalUniform.create(device);
        if (g_globalUniform.isReady()) {
            g_renderSystem->createUniformBuffer(device, g_globalUniform.getLayout());
            g_uniformCreated = g_renderSystem->isUniformReady();
        }

        // 재질 레이아웃이 이제 있으므로 시험용 텍스처를 만든다
        if (g_uniformCreated) {
            const uint8_t light[3] = {235, 235, 235};
            const uint8_t dark[3] = {60, 60, 70};
            std::shared_ptr<LotTexture> checker =
                LotTexture::createChecker(device, 64, 8, light, dark);
            g_checkerMaterial = std::make_shared<LotMaterial>(
                device, g_renderSystem->getMaterialLayout(), checker);
        }
    }

    // 3. 파이프라인 생성 (uniform 레이아웃 준비 후)
    if (!g_pipelineCreated && g_uniformCreated) {
        g_renderSystem->createPipeline(device,
                                       g_renderer->getSwapchain().getFormat(),
                                       g_renderer->getSwapchain().getDepthFormat());
        g_pipelineCreated = true;
    }

    // 3-1. 선/폴리라인/기즈모/글자 렌더 시스템. 글로벌 레이아웃만 있으면 되므로 메시 쪽과 독립이다.
    // (격자는 시스템이 아니라 lot_grid 가 프레임마다 선 시스템에 넣는다.)
    if (!g_overlayCreated && g_uniformCreated) {
        const WGPUTextureFormat color = g_renderer->getSwapchain().getFormat();
        const WGPUTextureFormat depth = g_renderer->getSwapchain().getDepthFormat();
        g_lineSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_polylineSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_gizmoSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_textSystem->create(device, g_globalUniform.getLayout(), color, depth);
        lot_text::setMeasurer(g_textSystem.get());  // 문자 피킹/박스 선택이 글자 폭을 물어본다
        lot_pick::setSelectableFilter(layerSelectable);  // 꺼지거나 잠긴 층은 안 잡힌다
        g_postSystem->create(device, color);  // 화면에 그리므로 스왑체인 포맷, 뎁스 없음
        g_overlayCreated = true;
    }

    // 4. 게임 오브젝트 생성 (한 번만)
    if (!g_gameObjectsCreated && g_renderSystem->isPipelineReady()) {
        createGameObjects();
        g_gameObjectsCreated = true;
    }

    // 4-1. OBJ 모델이 도착했으면 장면에 넣는다 (한 번만).
    //      큐브들은 그 전에 이미 그려지고 있다 - 로딩이 화면을 막지 않는다.
    if (!doc().objPlaced && g_objModel && g_gameObjectsCreated) {
        placeObjModel();
    }

    // 5. 리사이즈 처리.
    //    투영 행렬은 아래에서 매 프레임 현재 종횡비로 다시 만들므로 여기서는
    //    플래그만 내린다 (스왑체인/뎁스 버퍼 재생성은 스왑체인이 알아서 한다).
    if (g_renderer->wasWindowResized()) {
        g_renderer->resetWindowResizedFlag();
    }

    // 6. 렌더링
    if (g_renderer->beginFrame()) {
        // 시간 업데이트 (고정 0.016 대신 실제 경과 시간을 쓴다)
        const double nowMs = emscripten_get_now();
        const double deltaSec = (g_lastFrameMs > 0.0) ? (nowMs - g_lastFrameMs) / 1000.0 : 0.0;
        g_lastFrameMs = nowMs;
        g_time += deltaSec;

        // 선택 / 기즈모 드래그 / 스냅. 카메라가 갱신된 뒤에 해야 레이가 이번 프레임
        // 것과 맞지만, 한 프레임 차이는 눈에 띄지 않으므로 이전 프레임 카메라로 한다.
        {
            const auto& sc = g_renderer->getSwapchain();
            EditController::Context ctx{doc().camera, g_mouse, *g_gizmoSystem, doc().objects,
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight()),
                                        g_sketch.anyActive() || g_transform.isActive() || g_offset.isActive(),
                                        g_transform.isPreviewing(),
                                        g_transform.isActive() ? g_transform.referencePoint()
                                                               : g_sketch.referencePoint(),
                                        g_transform.isActive() ? nullptr : g_sketch.draftPoints()};
            doc().edit.update(ctx);

            // 변환 도구: 스냅을 쓰므로 편집기 뒤. 숫자 버퍼는 키 컨트롤러가 모은 것을 넘긴다.
            g_cameraController.setNumberCapture(g_transform.isPreviewing() || g_offset.wantsNumber());
            g_transform.setNumberBuffer(g_cameraController.numberBuffer());
            g_offset.setNumberBuffer(g_cameraController.numberBuffer());
            TransformTool::Context tctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(),
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_transform.update(tctx, doc().edit.history());
            if (const auto copies = g_transform.consumeCreated(); !copies.empty()) {
                doc().edit.setSelection(copies);  // 놓은 사본을 선택 - 이어서 기즈모로 다듬을 수 있게
            }
            // 간격띄우기: 변환 도구와 같은 자리 (클릭을 가져간다)
            OffsetTool::Context octx{doc().camera, g_mouse, doc().objects,
                                     static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight())};
            g_offset.update(octx, doc().edit.history());
            g_offset.consumeCreated();

            // 스케치는 편집기가 찾아둔 스냅을 쓰므로 그 뒤에 온다. 활성이면 클릭을 가져간다.
            SketchController::Context sctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(),
                                           static_cast<float>(sc.getWidth()),
                                           static_cast<float>(sc.getHeight())};
            g_sketch.update(sctx);
            if (const auto id = g_sketch.consumeCommittedId(); id != LotGameObject::kInvalidId) {
                // 새로 그린 것은 현재 층에
                if (auto* obj = LotGameObject::find(doc().objects, id)) obj->layer = doc().layers.current();
                doc().edit.history().recordCreated("sketch", doc().objects, id);
            }
            // 문자 도구가 기준점을 찍었으면 브라우저 입력창을 연다 (글자 입력은 DOM 이 받는다)
            if (g_sketch.consumeTextInputRequest()) {
                g_editingTextId = LotGameObject::kInvalidId;
                js_showTextInput("text, Enter to place", "");
            }
            // 문자를 더블 클릭하면 그 내용을 고친다
            if (const auto id = doc().edit.consumeDoubleClicked(); id != LotGameObject::kInvalidId) {
                if (const auto* obj = LotGameObject::find(doc().objects, id); obj && obj->isText()) {
                    g_editingTextId = id;
                    js_showTextInput("edit text, Enter to apply", obj->text.content.c_str());
                    LOT_LOG("text: editing object " << id);
                }
            }
        }

        // Z 키 · 메뉴 · 리본 · 명령행 zoom, 그리고 휠 더블 클릭 (AutoCAD 손짓)
        if (g_cameraController.consumeZoomExtents() | g_mouse.consumeMiddleDoubleClick()) {
            zoomExtents();
        }
        if (g_cameraController.consumeOrthoToggle()) {
            auto& s = lot_cursor::settings();
            s.ortho = !s.ortho;
            LOT_LOG("ortho tracking: " << (s.ortho ? "on" : "off"));
        }
        if (g_cameraController.consumeGridSnapToggle()) {
            auto& s = lot_cursor::settings();
            s.gridSpacing = (s.gridSpacing > 0.0f) ? 0.0f : doc().gridSpacing;
            LOT_LOG("grid snap: " << (s.gridSpacing > 0.0f ? "on" : "off")
                    << " (spacing " << doc().gridSpacing << ")");
        }
        if (g_cameraController.consumeOsnapToggle()) {
            auto& s = lot_cursor::settings();
            s.osnap = !s.osnap;
            LOT_LOG("osnap: " << (s.osnap ? "on" : "off"));
        }
        if (g_cameraController.consumePolarToggle()) {
            auto& s = lot_cursor::settings();
            s.polar = !s.polar;
            LOT_LOG("polar tracking: " << (s.polar ? "on" : "off"));
        }
        if (g_cameraController.consumeGridDisplayToggle()) {
            g_display.grid = !g_display.grid;
            LOT_LOG("display: grid " << (g_display.grid ? "on" : "off"));
        }
        if (const int d = g_cameraController.consumePolygonSidesDelta(); d != 0) {
            g_sketch.changePolygonSides(d);
        }
        g_cameraController.setCadMode(doc().camera.isCadMode());

        // 실행 취소 / 다시 실행. 스케치 중이면 도구부터 닫는다 - 반쯤 그린 것과 섞이지 않게.
        if (g_cameraController.consumeUndo()) {
            g_sketch.cancel();
            g_transform.cancel(doc().objects);
            g_offset.cancel();
            doc().edit.undo(doc().objects);
        }
        if (g_cameraController.consumeRedo()) {
            g_sketch.cancel();
            g_transform.cancel(doc().objects);
            g_offset.cancel();
            doc().edit.redo(doc().objects);
        }

        // 스케치 도구 시작 / 끝 / 취소. 도구를 열면 선택은 비운다 (Vulkan 쪽과 같다).
        if (const int tool = g_cameraController.consumeSketchTool(); tool >= 0) {
            g_transform.cancel(doc().objects);
            g_offset.cancel();
            doc().edit.clearSelection();
            g_sketch.start(static_cast<SketchController::Kind>(tool), doc().camera);
        }
        // 변환 도구 (기준점 방식). 선택이 있어야 한다.
        if (const int mode = g_cameraController.consumeTransformMode(); mode >= 0) {
            g_sketch.cancel();
            g_transform.cancel(doc().objects);
            g_offset.cancel();
            g_transform.start(static_cast<TransformTool::Mode>(mode + 1), doc().edit.selection(),
                              doc().camera, doc().objects);
        }
        if (g_cameraController.consumeEnter()) {
            if (g_offset.isActive()) {
                g_offset.finish();
                g_cameraController.clearNumberBuffer();
            } else if (g_transform.isActive()) {
                TransformTool::Context tctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(),
                                            static_cast<float>(g_renderer->getSwapchain().getWidth()),
                                            static_cast<float>(g_renderer->getSwapchain().getHeight())};
                g_transform.finish(tctx, doc().edit.history());
                g_cameraController.clearNumberBuffer();
            } else {
                g_sketch.finish(doc().objects);
            }
        }
        if (g_cameraController.consumeEscape()) {
            if (g_offset.isActive()) { g_offset.cancel(); g_cameraController.clearNumberBuffer(); }
            else if (g_transform.isActive()) g_transform.cancel(doc().objects);
            else if (g_sketch.anyActive()) { g_sketch.cancel(); js_hideTextInput(); }
            else doc().edit.clearSelection();
        }

        // 뷰 모드 전환 / 표준 뷰
        if (g_cameraController.consumeViewModeToggle()) {
            doc().camera.setViewMode(doc().camera.isCadMode() ? LotCamera::ViewMode::Fps
                                                      : LotCamera::ViewMode::Cad);
            LOT_LOG("view: " << (doc().camera.isCadMode() ? "cad orbit" : "fps"));
        }
        if (const int preset = g_cameraController.consumeViewPreset(); preset >= 0) {
            doc().camera.setViewMode(LotCamera::ViewMode::Cad);
            doc().camera.resetCadView(static_cast<LotCamera::CadViewType>(preset));
            static const char* kViewNames[] = {"front", "back", "top", "bottom",
                                               "right", "left", "isometric"};
            LOT_LOG("view: " << kViewNames[preset]);
        }

        // 마우스/키로 카메라를 움직인다. 델타와 휠은 모드와 관계없이 매 프레임
        // 비워야 한다 - 안 그러면 모드를 바꾼 순간 쌓인 값이 한꺼번에 들어간다.
        float mouseDx = 0.0f, mouseDy = 0.0f;
        g_mouse.consumeDelta(mouseDx, mouseDy);
        const float wheel = g_mouse.consumeWheel();
        if (doc().camera.isCadMode()) {
            // 우클릭 궤도, 중클릭 팬, 휠 줌, 화살표 궤도
            if (g_mouse.isRightDown()) {
                // 부호가 음수인 이유: 팬과 마찬가지로 '장면을 잡고 끄는' 느낌이어야 한다.
                // 오른쪽으로 끌면 장면이 오른쪽으로 돌아야 하므로 카메라는 왼쪽으로 간다.
                doc().camera.orbitAroundTarget(-mouseDx * kOrbitRadPerPixel, -mouseDy * kOrbitRadPerPixel);
            } else if (g_mouse.isMiddleDown()) {
                doc().camera.panTarget(mouseDx, mouseDy,
                                   static_cast<float>(g_renderer->getSwapchain().getHeight()));
            }
            float yaw = 0.0f, pitch = 0.0f;
            g_cameraController.orbitInput(yaw, pitch);
            if (yaw != 0.0f || pitch != 0.0f) {
                const float step = kOrbitRadPerSec * static_cast<float>(deltaSec);
                doc().camera.orbitAroundTarget(yaw * step, pitch * step);
            }
            if (wheel != 0.0f) {
                // 커서 기준 줌 (AutoCAD 와 같다): 커서 아래 점이 줌 전후로 같은 화면 자리에
                // 남도록 타깃을 옮긴다. 화면 중심에서 커서까지의 월드 길이는 배율만큼 줄어드니
                // 그 차이 (1 - 실제 배율) 만큼 커서 쪽으로. 원근은 타깃 깊이의 평면 기준이다.
                const float vh = static_cast<float>(g_renderer->getSwapchain().getHeight());
                const float vw = static_cast<float>(g_renderer->getSwapchain().getWidth());
                const float wppBefore = doc().camera.worldPerPixel(doc().camera.getTarget(), vh);
                float applied = 1.0f;   // 한계에 걸리면 덜 줌된다 - 그 실제 배율
                if (doc().orthographic) {
                    const float before = doc().orthoHalfHeight;
                    doc().orthoHalfHeight *= LotCamera::zoomFactor(wheel);
                    if (doc().orthoHalfHeight < doc().orthoMinHalfHeight) doc().orthoHalfHeight = doc().orthoMinHalfHeight;
                    if (doc().orthoHalfHeight > doc().orthoMaxHalfHeight) doc().orthoHalfHeight = doc().orthoMaxHalfHeight;
                    applied = doc().orthoHalfHeight / before;
                } else {
                    const float before = doc().camera.getOrbitDistance();
                    doc().camera.zoomToTarget(wheel);
                    applied = doc().camera.getOrbitDistance() / before;
                }
                const float dx = g_mouse.x() - vw * 0.5f;
                const float dy = g_mouse.y() - vh * 0.5f;
                const vec3 offset = (doc().camera.getRight() * dx + doc().camera.getDown() * dy) * wppBefore;
                doc().camera.setTarget(doc().camera.getTarget() + offset * (1.0f - applied));
            }
        } else {
            // 1인칭: 키 입력을 뷰어 오브젝트에 반영한 뒤, 그 위치/회전으로 뷰 행렬을 만든다.
            // 첫 프레임은 deltaSec 이 0 이라 아무 일도 일어나지 않는다.
            g_cameraController.moveInPlaneXY(static_cast<float>(deltaSec), doc().viewer);
        }

        // 투영 전환 / 직교 줌
        if (const int mode = g_cameraController.consumeGizmoMode(); mode >= 0) {
            g_gizmoSystem->mode = static_cast<GizmoRenderSystem::Mode>(mode);
            static const char* kModeNames[] = {"move", "rotate", "scale"};
            LOT_LOG("gizmo: " << kModeNames[mode]);
        }
        // 도구가 열려 있으면 편집 키를 무시한다 (플래그는 비워야 나중에 튀어나오지 않는다)
        const bool editKeysEnabled = !g_sketch.anyActive() && !g_transform.isActive();
        if (g_cameraController.consumeDuplicate() && editKeysEnabled) {
            doc().edit.duplicateSelection(doc().objects);
        }
        if (g_cameraController.consumeSelectAll() && editKeysEnabled) {
            doc().edit.selectAll(doc().objects);
        }
        if (g_cameraController.consumeDelete() && editKeysEnabled) {
            doc().edit.deleteSelection(doc().objects);
        }
        if (g_cameraController.consumeOutlineToggle()) {
            g_postSystem->mode = (g_postSystem->mode == PostProcessSystem::Mode::Outline)
                ? PostProcessSystem::Mode::Passthrough : PostProcessSystem::Mode::Outline;
            LOT_LOG("post: " << (g_postSystem->mode == PostProcessSystem::Mode::Outline
                                 ? "outline" : "passthrough"));
        }
        if (g_cameraController.consumeProjectionToggle()) {
            doc().orthographic = !doc().orthographic;
            LOT_LOG("projection: " << (doc().orthographic ? "orthographic" : "perspective"));
        }
        if (doc().orthographic) {
            // 지수적으로 줄이고 키워야 어느 배율에서든 같은 '느낌'으로 줌된다
            const int zoom = g_cameraController.zoomDirection();
            if (zoom != 0) {
                const float factor = std::exp(-zoom * kOrthoZoomSpeed * static_cast<float>(deltaSec));
                doc().orthoHalfHeight *= factor;
                if (doc().orthoHalfHeight < doc().orthoMinHalfHeight) doc().orthoHalfHeight = doc().orthoMinHalfHeight;
                if (doc().orthoHalfHeight > doc().orthoMaxHalfHeight) doc().orthoHalfHeight = doc().orthoMaxHalfHeight;
            }
        }

        // 메뉴 · 리본 · 레이어 패널 (바뀐 것만 DOM 으로 나간다)
        {
            lot_ui::State ui;
            ui.gizmoMode = static_cast<int>(g_gizmoSystem->mode);
            ui.sketchTool = g_sketch.activeKind();
            ui.xformMode = g_transform.isActive() ? g_transform.modeIndex() - 1 : -1;
            ui.view = doc().camera.presetViewIndex();
            ui.fps = !doc().camera.isCadMode();
            ui.ortho = doc().orthographic;
            ui.orthoTracking = lot_cursor::settings().ortho;
            ui.gridSnap = lot_cursor::settings().gridSpacing > 0.0f;
            ui.osnap = lot_cursor::settings().osnap;
            ui.polar = lot_cursor::settings().polar;
            ui.gridShow = g_display.grid;
            ui.dims = g_display.dims;
            ui.visualStyle = g_display.style;
            ui.osnapKinds = lot_osnap::enabledKinds();
            ui.outline = g_postSystem->mode == PostProcessSystem::Mode::Outline;
            ui.canUndo = doc().edit.history().canUndo();
            ui.canRedo = doc().edit.history().canRedo();
            ui.hint = g_offset.isActive() ? g_offset.hint()
                    : g_transform.isActive() ? g_transform.hint() : g_sketch.hint();
            ui.offset = g_offset.isActive();
            std::vector<lot_ui::DocTab> tabs;
            tabs.reserve(g_documents.size());
            for (const auto& d : g_documents) tabs.push_back({d->name, d->modified()});
            g_ui.update(ui, doc().layers, doc().objects, doc().edit, tabs,
                        static_cast<int>(g_documentIndex));
            g_ui.updateViewCube(doc().camera);
        }

        // 카메라 갱신. 종횡비는 매 프레임 현재 값으로 넣어두면
        // 리사이즈를 따로 챙기지 않아도 항상 맞는다.
        if (doc().orthographic) {
            doc().camera.setOrthographicProjection(doc().orthoHalfHeight, g_renderer->getAspectRatio(),
                                               doc().nearZ, doc().farZ);
        } else {
            doc().camera.setPerspectiveProjection(kFovY, g_renderer->getAspectRatio(), doc().nearZ, doc().farZ);
        }
        if (doc().camera.isCadMode()) {
            doc().camera.updateCadView();
        } else {
            doc().camera.setViewFromTransform(doc().viewer.transform.translation,
                                          doc().viewer.transform.rotation);
        }

        // (예전의 자동 회전은 뺐다 - 회전/축척 기즈모로 편집한 값을 매 프레임
        //  덮어쓰기 때문이다. 초기 자세는 createGameObjects / placeObjModel 에서 준다.)

        // 광원. 공전 표시가 붙은 것만 돌린다 (옮기면 꺼진다). 조명 유니폼은
        // 씬에 있는 첫 광원 오브젝트에서 읽는다 - 지우면 주변광만 남는다.
        {
            const float lightAngle = static_cast<float>(g_time) * kLightOrbitSpeed;
            g_lighting.pointLight.intensity = 0.0f;
            for (auto& entry : doc().objects) {
                LotGameObject& obj = entry.second;
                if (!obj.isLight()) continue;
                if (obj.light.orbit) {
                    obj.transform.translation =
                        vec3(std::cos(lightAngle) * kLightSwingX,
                             kLightBaseY + std::sin(lightAngle) * kLightSwingY, kLightHeight);
                }
                if (g_lighting.pointLight.intensity == 0.0f) {
                    g_lighting.pointLight.position = obj.transform.translation;
                    g_lighting.pointLight.color = obj.light.color;
                    g_lighting.pointLight.intensity = obj.light.intensity;
                }
            }
        }

        // 프레임당 유니폼 갱신. 렌더 시스템 전부가 같은 값을 본다.
        g_globalUniform.update(doc().camera, g_lighting);

        // 패스 1: 장면을 오프스크린 타깃에. 스왑체인과 같은 크기/포맷으로 맞춘다.
        const auto& sc = g_renderer->getSwapchain();
        // 크기는 이번 프레임에 받은 화면 텍스처 것 - 프레임 도중 크기가 바뀌어도 패스 3 에서
        // 화면 텍스처와 이 뎁스가 같은 크기여야 한다
        g_sceneTarget.ensureSize(device, sc.currentImageWidth(), sc.currentImageHeight(),
                                 sc.getFormat(), sc.getDepthFormat());
        WGPURenderPassEncoder scenePass = g_sceneTarget.beginRenderPass(
            g_renderer->getCurrentEncoder(), WGPUColor{0.1, 0.1, 0.1, 1.0});

        // 이번 프레임 묶음. 렌더 시스템은 이것 하나만 받는다.
        FrameInfo frame{
            static_cast<float>(deltaSec),
            scenePass,
            doc().camera,
            g_globalUniform.getBindGroup(),
            doc().objects,
            layerVisible,  // 꺼진 층의 메시는 건너뛴다
            displayColor,  // '층 따름'이면 층 색
        };

        // 이번 프레임의 보조선.
        g_lineSystem->clear();
        // 바닥 격자. 그리드 스냅(F9) 과 같은 간격으로, 카메라를 따라다닌다.
        if (g_display.grid) {
            lot_grid::draw(*g_lineSystem, doc().camera, doc().gridSpacing, static_cast<float>(sc.getHeight()));
        }

        // 선택 상자 / 박스 선택 사각형 / 스냅 마커
        {
            EditController::Context ctx{doc().camera, g_mouse, *g_gizmoSystem, doc().objects,
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            doc().edit.drawOverlay(*g_lineSystem, ctx);
        }

        // 스케치 오브젝트 + 치수 + 그리는 중인 프리뷰
        g_polylineSystem->clear();
        g_textSystem->clear();
        for (const auto& entry : doc().objects) {
            const LotGameObject& obj = entry.second;
            if (!layerVisible(obj)) continue;  // 꺼진 층
            const vec3 color = displayColor(obj);
            if (obj.isSketch()) {
                const uint32_t lt = displayLinetype(obj);
                if (lt == lot_linetype::kContinuous) {
                    g_polylineSystem->addPolyline(obj.worldPoints(), color, obj.closed);
                } else {
                    // 무늬가 있으면 선분으로 잘라 낸다. 화면에서 한 주기가 4px 보다
                    // 짧아지면 (줌 아웃) 실선으로 떨어뜨려 뭉개지지 않게.
                    const float minDash = doc().camera.worldPerPixel(obj.transform.translation,
                                                                 static_cast<float>(sc.getHeight())) * 4.0f;
                    lot_linetype::emit(*g_lineSystem, obj.worldPoints(), obj.closed, color, lt,
                                       doc().linetypeScale, minDash);
                }
            } else if (obj.isDimension()) {
                if (g_display.dims) lot_dim::draw(obj, doc().camera, *g_lineSystem, *g_textSystem, color);
            } else if (obj.isText()) {
                if (textWorthDrawing(obj, static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight()))) {
                    lot_text::draw(obj, *g_textSystem, color);
                }
            } else if (obj.isLight()) {
                g_lineSystem->addCross(obj.transform.translation, obj.light.markerSize, color);
            } else if (obj.model && g_display.style == DisplaySettings::WireMesh) {
                // 와이어프레임: 면 대신 삼각형 변을 전부 (색은 오브젝트 색)
                const mat4 m = obj.transform.mat4Transform();
                const std::vector<vec3>& pos = obj.model->getPositions();
                const std::vector<uint32_t>& idx = obj.model->getIndices();
                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    const vec3 a = transformPoint(m, pos[idx[i]]);
                    const vec3 b = transformPoint(m, pos[idx[i + 1]]);
                    const vec3 c = transformPoint(m, pos[idx[i + 2]]);
                    g_lineSystem->addLine(a, b, color);
                    g_lineSystem->addLine(b, c, color);
                    g_lineSystem->addLine(c, a, color);
                }
            } else if (obj.model && g_display.featureEdges() && !obj.model->getFeatureEdges().empty()) {
                // 메시 모서리 (네이티브의 ShadedEdge). 면 위에 딱 붙은 선은 뎁스가 면과 같아
                // 깜빡이므로 카메라 쪽으로 조금 당긴다 - 거리의 0.2%, 화면에서는 안 보이는 만큼.
                const mat4 m = obj.transform.mat4Transform();
                const vec3 eye = doc().camera.getPosition();
                const std::vector<vec3>& e = obj.model->getFeatureEdges();
                for (size_t i = 0; i + 1 < e.size(); i += 2) {
                    vec3 a = transformPoint(m, e[i]);
                    vec3 b = transformPoint(m, e[i + 1]);
                    a = a + (eye - a) * 0.002f;
                    b = b + (eye - b) * 0.002f;
                    // 면이 칠해진 셰이딩+엣지는 검은 테두리, 면이 없거나 배경색이면 오브젝트 색
                    g_lineSystem->addLine(a, b, g_display.style == DisplaySettings::ShadedEdges ? kMeshEdgeColor : color);
                }
            }
        }
        {
            SketchController::Context sctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(),
                                           static_cast<float>(sc.getWidth()),
                                           static_cast<float>(sc.getHeight())};
            g_sketch.drawPreview(*g_polylineSystem, *g_lineSystem, *g_textSystem, sctx);
            TransformTool::Context tctx{doc().camera, g_mouse, doc().objects, doc().edit.snap(),
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_transform.drawOverlay(*g_lineSystem, tctx);
            OffsetTool::Context octx{doc().camera, g_mouse, doc().objects,
                                     static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight())};
            g_offset.drawOverlay(*g_lineSystem, octx);
        }

        // 패스 1 에는 메시만. 격자/보조선/기즈모는 후처리에 걸리면 안 되므로
        // (선 하나하나가 뎁스 불연속이라 전부 외곽선으로 잡힌다) 패스 3 으로 미룬다.
        if (g_display.fillFaces()) {
            g_renderSystem->flatFill = (g_display.style == DisplaySettings::HiddenLine);
            g_renderSystem->render(frame);
        }

        if (scenePass) {
            wgpuRenderPassEncoderEnd(scenePass);
            wgpuRenderPassEncoderRelease(scenePass);
        }

        // 패스 2: 오프스크린 결과를 화면으로. 후처리가 색/뎁스를 읽어 효과를 얹는다.
        // 화면을 통째로 덮어쓰므로 뎁스 어태치먼트가 필요 없다.
        g_renderer->beginRenderPass(/*withDepth=*/false);
        g_postSystem->render(g_renderer->getCurrentRenderPass(), g_sceneTarget);
        g_renderer->endRenderPass();

        // 패스 3: 오버레이. 후처리 결과 위에 도구 지오메트리를 덧그린다.
        // 뎁스는 장면 것을 그대로 쓰므로 격자가 메시 뒤로 제대로 가려진다.
        g_renderer->beginOverlayPass(g_sceneTarget.getDepthView());
        frame.pass = g_renderer->getCurrentRenderPass();
        g_lineSystem->render(frame);
        g_polylineSystem->render(frame);
        g_textSystem->render(frame);  // 반투명 - 불투명한 것들 뒤에
        // 기즈모는 뎁스를 무시하므로 맨 마지막. 선택이 있을 때만.
        doc().edit.drawGizmo(frame, *g_gizmoSystem);
        g_renderer->endRenderPass();

        // 프레임 종료
        g_renderer->endFrame();
    }
}

// 메인 함수
int main() {
    LOT_LOG("==================================");
    LOT_LOG("WebGPU 3D Engine - Perspective Cubes");
    LOT_LOG("==================================");

    // 첫 도면. doc() 을 건드리는 것은 전부 이 뒤에 온다.
    // 시연용 큐브·광원·토러스는 이 도면에만 들어간다 (새 도면은 빈 종이).
    g_documents.push_back(makeDocument());
    doc().objPlaced = false;

    // Renderer 생성 (동적 크기 - 브라우저 창 크기에 맞춤)
    g_renderer = std::make_unique<LotWebRenderer>();
    g_renderer->init();

    // Render System 생성 (Pipeline + Uniform 관리)
    g_renderSystem = std::make_unique<SimpleRenderSystem>("shaders/triangle.wgsl");
    g_lineSystem = std::make_unique<LineRenderSystem>();
    g_polylineSystem = std::make_unique<PolylineRenderSystem>();
    g_gizmoSystem = std::make_unique<GizmoRenderSystem>();
    g_postSystem = std::make_unique<PostProcessSystem>();
    g_textSystem = std::make_unique<TextRenderSystem>();

    g_cameraController.init();
    // 마우스는 캔버스가 생긴 뒤에 (렌더 루프 1 단계) 등록한다

    // OBJ 로더
    js_setupObjFileInput();

    LOT_LOG("Renderer initialized (fullscreen canvas).");

    // 렌더 루프 시작
    emscripten_set_main_loop(renderLoop, 0, 1);

    return 0;
}
