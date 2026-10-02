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
#include "lot_json.h"
#include "lot_layers.h"
#include "lot_cursor_snap.h"
#include "lot_dxf.h"
#include "ui/lot_ui.h"
#include "lot_cursor_snap.h"
#include "lot_linetype.h"
#include "lot_sketch_tool.h"
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
    // 파일 선택창은 사용자 제스처로만 열 수 있어서 JS 쪽에 버튼을 만든다.
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
LotGameObject::Map g_gameObjects;
LotCamera g_camera;
KeyboardMovementController g_cameraController;
MouseInput g_mouse;
SketchController g_sketch;
LotLayers g_layers;
lot_ui::LotUi g_ui;
TransformTool g_transform;

// 선택/기즈모 드래그/스냅. 상호작용은 전부 여기로 모였다.
EditController g_edit;

// 카메라의 위치와 회전을 담아두는 오브젝트. 모델이 없으므로 그려지지 않는다.
// 카메라를 게임 오브젝트처럼 다루면 나중에 다른 오브젝트에 붙이기도 쉽다.
LotGameObject g_viewerObject = LotGameObject::createGameObject();

// 애니메이션 시간
static double g_time = 0.0;
static double g_lastFrameMs = 0.0;

// 초기화 상태
static bool g_modelCreated = false;
static bool g_objRequested = false;   // fetch 를 시작했는지 (한 번만 보낸다)
static bool g_objPlaced = false;      // 받아온 모델을 장면에 넣었는지

// OBJ 모델이 들어가 있는 오브젝트. 파일을 새로 열면 이 오브젝트의 모델을 갈아끼운다.
// 인덱스가 아니라 id 라서 다른 오브젝트가 지워져도 어긋나지 않는다.
static LotGameObject::id_t g_objObjectId = LotGameObject::kInvalidId;

// 불러온 모델이 화면에 차는 크기. 남이 만든 OBJ 는 단위가 제각각이라
// (몇 백 단위짜리도 흔하다) 파일 값을 그대로 쓰면 안 보이거나 화면을 덮는다.
static const float kObjTargetSize = 1.4f;
static const float kObjHeight = 0.6f;       // 가운데 모델의 중심 높이 (바닥 위)
static const float kCubeHalf = 0.3f;        // 큐브 반 변 (scale 0.6) - 바닥에 딱 얹히게
static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_overlayCreated = false;
static bool g_gameObjectsCreated = false;

// 카메라 설정
static const float kFovY = 50.0f * 3.14159265f / 180.0f;
// 클립 평면은 씬 크기에 따라 움직인다 (zoomExtents 가 정한다). 기본값은 미터 단위
// 장난감 씬용이고, mm 도면을 열면 수천 배로 늘어난다. near 를 같이 키워야 깊이
// 정밀도가 남는다 (near 0.1 에 far 100000 이면 z-fighting 이 심하다).
static float g_nearZ = 0.1f;
static float g_farZ = 100.0f;
static const vec3 kCameraStartPosition{0.0f, -2.5f, 0.6f};  // (FPS 모드) 앞(-Y)에서 눈높이로

// CAD 궤도 조작 감도
static const float kOrbitRadPerPixel = 0.005f;  // 우클릭 드래그 1px 당 (한 바퀴 ≈ 1250px)
static const float kOrbitRadPerSec = 1.5f;      // 화살표로 돌릴 때

// 투영 모드. P 키로 전환한다.
// 직교의 halfHeight 는 화면 세로 절반에 담기는 월드 길이 - 곧 줌이다.
static bool g_orthographic = false;
// F8 직교 / F9 그리드 스냅. 실제 값은 lot_cursor::settings() 한 벌 - 도구들이 그걸 읽는다.
// 그리드 간격은 씬 크기에 맞춰 정한다 (zoomExtents).
static float g_gridSpacing = 0.5f;
// 전역 선종류 축척 (AutoCAD 의 LTSCALE). 씬을 열 때 크기에 맞춰 잡는다.
static float g_linetypeScale = 1.0f;
static float g_orthoHalfHeight = 1.6f;
static const float kOrthoZoomSpeed = 2.0f;      // 초당 배율
static float g_orthoMinHalfHeight = 0.2f;
static float g_orthoMaxHalfHeight = 20.0f;

// 조명. 광원은 큐브들 위쪽 앞에 두고 천천히 돌린다.
// 감쇠가 거리 제곱에 반비례하므로 세기는 거리의 제곱 규모로 잡아야 한다
// (거리 1.5 면 감쇠가 1/2.25 이라, 세기 4 정도는 되어야 눈에 찬다).
static SceneLighting g_lighting = [] {
    SceneLighting lighting;
    lighting.ambientIntensity = 0.03f;
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

        cube.model = g_cubeModel;
        cube.material = g_checkerMaterial;
        cube.transform.translation = translation;
        cube.transform.scale = vec3(0.6f);
        // 세 면이 다 보이게 위 축(Z) 둘레로 살짝 돌린다 (바닥에 얹힌 채로)
        cube.transform.rotation = quat::angleAxis(0.6f, vec3(0.0f, 0.0f, 1.0f));

        const auto id = cube.getId();
        g_gameObjects.emplace(id, std::move(cube));
    }

    LOT_LOG("Game objects created: " << g_gameObjects.size());
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
    g_objObjectId = object.getId();
    g_gameObjects.emplace(g_objObjectId, std::move(object));

    g_objPlaced = true;
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

// 층 표를 보는 콜백들. 렌더/피킹 시스템은 층을 모르고 이 함수만 부른다.
static bool layerVisible(const LotGameObject& obj) { return g_layers.isVisible(obj.layer); }
static bool layerSelectable(const LotGameObject& obj) { return g_layers.isSelectable(obj.layer); }

// 오브젝트가 쓸 선종류. '층 따름'이면 층의 것.
static uint32_t displayLinetype(const LotGameObject& obj) {
    if (obj.linetype != lot_linetype::kByLayer) return obj.linetype;
    const LotLayers::Layer* l = g_layers.find(obj.layer);
    return l ? l->linetype : lot_linetype::kContinuous;
}

// 오브젝트가 화면에 낼 색. '층 따름'이면 층 색.
static vec3 displayColor(const LotGameObject& obj) {
    if (!obj.colorByLayer) return obj.color;
    const LotLayers::Layer* l = g_layers.find(obj.layer);
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
        if (g_transform.isPreviewing()) {
            g_transform.setNumberBuffer(text);
            const auto& sc = g_renderer->getSwapchain();
            TransformTool::Context tctx{g_camera, g_mouse, g_gameObjects, g_edit.snap(),
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_transform.finish(tctx, g_edit.history());
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
                              g_layers, g_gameObjects, g_edit);
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
        auto* obj = LotGameObject::find(g_gameObjects, id);
        if (!obj || !obj->isText() || obj->text.content == content) return;
        EditHistory::Edit edit;
        edit.label = "text edit";
        edit.before = EditHistory::snapshot(g_gameObjects, std::set<LotGameObject::id_t>{id});
        if (content.empty()) {
            g_gameObjects.erase(id);  // 비우면 지운다 (CAD 관례)
            LOT_LOG("text: object " << id << " removed (empty)");
        } else {
            obj->text.content = content;
            edit.after = EditHistory::snapshot(g_gameObjects, std::set<LotGameObject::id_t>{id});
            LOT_LOG("text: object " << id << " edited (\"" << content << "\")");
        }
        if (edit.after.empty() && !content.empty()) return;
        g_edit.history().record(std::move(edit));
        return;
    }

    g_sketch.submitText(content, g_gameObjects);
    if (const auto id = g_sketch.consumeCommittedId(); id != LotGameObject::kInvalidId) {
        if (auto* obj = LotGameObject::find(g_gameObjects, id)) obj->layer = g_layers.current();
        g_edit.history().recordCreated("text", g_gameObjects, id);
    }
}

// 문자 입력창에서 Esc (또는 포커스 잃음).
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onTextCancelled() {
    g_editingTextId = LotGameObject::kInvalidId;
    if (g_sketch.waitingForTextInput()) g_sketch.cancel();
}

// 보고 있는 자리에 큐브 하나. 기본 씬의 큐브와 같은 모델/재질을 쓴다 (정점은 GPU 에
// 한 번만 올라가 있다). 자리는 카메라가 보는 점의 XY, 높이는 바닥에 딱 얹히게.
static void addCube() {
    if (!g_cubeModel) {
        LOT_ERR("cube: the cube model is not ready yet");
        return;
    }
    const vec3 look = g_camera.getTarget();
    auto cube = LotGameObject::createGameObject();
    cube.model = g_cubeModel;
    cube.material = g_checkerMaterial;
    cube.transform.translation = vec3(look.x, look.y, kCubeHalf);
    cube.transform.scale = vec3(0.6f);
    cube.layer = g_layers.current();
    const auto id = cube.getId();
    g_gameObjects.emplace(id, std::move(cube));
    g_edit.history().recordCreated("cube", g_gameObjects, id);
    g_edit.setSelection({id});   // 바로 기즈모로 옮길 수 있게
    LOT_LOG("cube: added object " << id << " at (" << look.x << ", " << look.y << ")");
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

// 전체 보기 (Zoom Extents). 모든 오브젝트의 월드 경계를 구해 카메라를 맞춘다.
// 클립 평면과 직교 줌 한계도 그 크기에 맞춘다 - 씬 단위가 m 든 mm 든 보이게.
static void zoomExtents() {
    vec3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f};
    bool any = false;
    auto grow = [&](const vec3& p) {
        if (!any) { lo = hi = p; any = true; return; }
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    };
    for (const auto& entry : g_gameObjects) {
        const LotGameObject& obj = entry.second;
        if (obj.isSketch()) {
            for (const vec3& p : obj.worldPoints()) grow(p);
        } else if (obj.isDimension()) {
            for (const vec3& p : lot_dim::outlinePoints(obj)) grow(p);
        } else if (obj.isText()) {
            vec3 c[4];
            if (lot_text::quadCorners(obj, c)) for (int i = 0; i < 4; ++i) grow(c[i]);
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
    if (!any) return;

    const vec3 center = (lo + hi) * 0.5f;
    const vec3 half = (hi - lo) * 0.5f;
    const float radius = std::fmax(std::sqrt(dot(half, half)), 0.01f);

    g_camera.setViewMode(LotCamera::ViewMode::Cad);
    g_camera.frame(center, radius, kFovY);

    // 직교: 세로 절반에 반지름이 담기게. 한계도 씬 크기에 비례.
    g_orthoHalfHeight = radius * 1.15f;
    g_orthoMaxHalfHeight = std::fmax(20.0f, radius * 20.0f);
    g_orthoMinHalfHeight = std::fmin(0.2f, radius * 0.001f);

    // 클립 평면: far 는 최대 궤도 거리 + 씬 반지름을 덮고, near 는 far 의 1e-5 이상.
    g_farZ = std::fmax(100.0f, g_camera.getMaxOrbitDistance() + radius * 2.0f);
    g_nearZ = std::fmax(0.01f, g_farZ * 1e-5f);

    // 치수 글자/화살표 기본 크기를 씬에 맞춘다 (mm 도면에서 0.3 짜리 글자는 안 보인다)
    g_sketch.setDimensionStyle(radius * 0.05f, radius * 0.025f);
    g_sketch.setTextHeight(radius * 0.07f);
    // 선종류 무늬도 씬 크기에 맞춘다 (mm 도면에서 0.5 짜리 파선은 실선처럼 보인다)
    g_linetypeScale = std::fmax(0.01f, radius * 0.25f);
    // 그리드 스냅 간격도 - 화면 가로에 눈금이 20 개쯤 되게 '보기 좋은' 값으로
    g_gridSpacing = lot_cursor::niceSpacing(radius * 0.1f);
    if (lot_cursor::settings().gridSpacing > 0.0f) lot_cursor::settings().gridSpacing = g_gridSpacing;

    LOT_LOG("view: zoom extents - center (" << center.x << ", " << center.y << ", " << center.z
            << ") radius " << radius << ", clip " << g_nearZ << " .. " << g_farZ);
}

// DXF 열기. JS 가 코드페이지를 풀어 UTF-8 로 넘긴다 (옛 도면은 CP949 등).
// 버퍼는 JS 가 malloc 으로 잡은 것이라 여기서 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onDxfFileLoaded(const char* data, int length) {
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

    g_sketch.cancel();
    g_transform.cancel(g_gameObjects);
    g_edit.clearSelection();
    g_edit.history().clear();
    g_gameObjects = std::move(loaded);
    g_layers = std::move(loadedLayers);
    g_objPlaced = true;  // 도면에는 기본 토러스를 끼워 넣지 않는다
    zoomExtents();
}

// 씬 저장. JS 가 파일로 내려준다. 문자열은 malloc 으로 잡아 넘기고 JS 가 free 한다.
extern "C" EMSCRIPTEN_KEEPALIVE
char* lot_saveScene() {
    const std::string text = lot_scene::save(g_gameObjects, g_layers);
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// 씬을 DXF 로. 2D 스케치/문자/치수만 나간다 (메시는 .lot 으로 저장한다).
extern "C" EMSCRIPTEN_KEEPALIVE
char* lot_saveDxf() {
    const std::string text = lot_dxf::save(g_gameObjects, g_layers, g_linetypeScale);
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// 씬 열기. 현재 씬을 버리고 파일 것으로 바꾼다 (히스토리/선택도 비운다).
// data 는 JS 가 malloc 으로 잡아 넘긴 버퍼라 여기서 해제한다.
extern "C" EMSCRIPTEN_KEEPALIVE
void lot_onLotFileLoaded(const char* data, int length) {
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
    const lot_scene::LoadStats stats =
        lot_scene::load(text, g_renderer->getDevice(), g_checkerMaterial, loaded, loadedLayers);
    if (!stats.error.empty()) {
        LOT_ERR(stats.error);
        return;
    }

    g_sketch.cancel();
    g_transform.cancel(g_gameObjects);
    g_edit.clearSelection();
    g_edit.history().clear();
    g_gameObjects = std::move(loaded);
    g_layers = std::move(loadedLayers);
    g_objPlaced = true;  // 파일 씬에는 기본 토러스를 끼워 넣지 않는다
    zoomExtents();       // 단위가 다른 파일(mm 도면)도 바로 보이게
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

    if (auto* object = LotGameObject::find(g_gameObjects, g_objObjectId)) {
        object->model = g_objModel;
        object->transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
        LOT_LOG("OBJ open: replaced the model at scene center");
    } else if (g_objPlaced) {
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
    if (!g_objPlaced && g_objModel && g_gameObjectsCreated) {
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
            EditController::Context ctx{g_camera, g_mouse, *g_gizmoSystem, g_gameObjects,
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight()),
                                        g_sketch.anyActive() || g_transform.isActive(),
                                        g_transform.isPreviewing(),
                                        g_transform.isActive() ? g_transform.referencePoint()
                                                               : g_sketch.referencePoint()};
            g_edit.update(ctx);

            // 변환 도구: 스냅을 쓰므로 편집기 뒤. 숫자 버퍼는 키 컨트롤러가 모은 것을 넘긴다.
            g_cameraController.setNumberCapture(g_transform.isPreviewing());
            g_transform.setNumberBuffer(g_cameraController.numberBuffer());
            TransformTool::Context tctx{g_camera, g_mouse, g_gameObjects, g_edit.snap(),
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_transform.update(tctx, g_edit.history());
            if (const auto copies = g_transform.consumeCreated(); !copies.empty()) {
                g_edit.setSelection(copies);  // 놓은 사본을 선택 - 이어서 기즈모로 다듬을 수 있게
            }

            // 스케치는 편집기가 찾아둔 스냅을 쓰므로 그 뒤에 온다. 활성이면 클릭을 가져간다.
            SketchController::Context sctx{g_camera, g_mouse, g_gameObjects, g_edit.snap(),
                                           static_cast<float>(sc.getWidth()),
                                           static_cast<float>(sc.getHeight())};
            g_sketch.update(sctx);
            if (const auto id = g_sketch.consumeCommittedId(); id != LotGameObject::kInvalidId) {
                // 새로 그린 것은 현재 층에
                if (auto* obj = LotGameObject::find(g_gameObjects, id)) obj->layer = g_layers.current();
                g_edit.history().recordCreated("sketch", g_gameObjects, id);
            }
            // 문자 도구가 기준점을 찍었으면 브라우저 입력창을 연다 (글자 입력은 DOM 이 받는다)
            if (g_sketch.consumeTextInputRequest()) {
                g_editingTextId = LotGameObject::kInvalidId;
                js_showTextInput("text, Enter to place", "");
            }
            // 문자를 더블 클릭하면 그 내용을 고친다
            if (const auto id = g_edit.consumeDoubleClicked(); id != LotGameObject::kInvalidId) {
                if (const auto* obj = LotGameObject::find(g_gameObjects, id); obj && obj->isText()) {
                    g_editingTextId = id;
                    js_showTextInput("edit text, Enter to apply", obj->text.content.c_str());
                    LOT_LOG("text: editing object " << id);
                }
            }
        }

        if (g_cameraController.consumeZoomExtents()) {
            zoomExtents();
        }
        if (g_cameraController.consumeOrthoToggle()) {
            auto& s = lot_cursor::settings();
            s.ortho = !s.ortho;
            LOT_LOG("ortho tracking: " << (s.ortho ? "on" : "off"));
        }
        if (g_cameraController.consumeGridSnapToggle()) {
            auto& s = lot_cursor::settings();
            s.gridSpacing = (s.gridSpacing > 0.0f) ? 0.0f : g_gridSpacing;
            LOT_LOG("grid snap: " << (s.gridSpacing > 0.0f ? "on" : "off")
                    << " (spacing " << g_gridSpacing << ")");
        }
        if (const int d = g_cameraController.consumePolygonSidesDelta(); d != 0) {
            g_sketch.changePolygonSides(d);
        }
        g_cameraController.setCadMode(g_camera.isCadMode());

        // 실행 취소 / 다시 실행. 스케치 중이면 도구부터 닫는다 - 반쯤 그린 것과 섞이지 않게.
        if (g_cameraController.consumeUndo()) {
            g_sketch.cancel();
            g_transform.cancel(g_gameObjects);
            g_edit.undo(g_gameObjects);
        }
        if (g_cameraController.consumeRedo()) {
            g_sketch.cancel();
            g_transform.cancel(g_gameObjects);
            g_edit.redo(g_gameObjects);
        }

        // 스케치 도구 시작 / 끝 / 취소. 도구를 열면 선택은 비운다 (Vulkan 쪽과 같다).
        if (const int tool = g_cameraController.consumeSketchTool(); tool >= 0) {
            g_transform.cancel(g_gameObjects);
            g_edit.clearSelection();
            g_sketch.start(static_cast<SketchController::Kind>(tool), g_camera);
        }
        // 변환 도구 (기준점 방식). 선택이 있어야 한다.
        if (const int mode = g_cameraController.consumeTransformMode(); mode >= 0) {
            g_sketch.cancel();
            g_transform.cancel(g_gameObjects);
            g_transform.start(static_cast<TransformTool::Mode>(mode + 1), g_edit.selection(),
                              g_camera, g_gameObjects);
        }
        if (g_cameraController.consumeEnter()) {
            if (g_transform.isActive()) {
                TransformTool::Context tctx{g_camera, g_mouse, g_gameObjects, g_edit.snap(),
                                            static_cast<float>(g_renderer->getSwapchain().getWidth()),
                                            static_cast<float>(g_renderer->getSwapchain().getHeight())};
                g_transform.finish(tctx, g_edit.history());
                g_cameraController.clearNumberBuffer();
            } else {
                g_sketch.finish(g_gameObjects);
            }
        }
        if (g_cameraController.consumeEscape()) {
            if (g_transform.isActive()) g_transform.cancel(g_gameObjects);
            else if (g_sketch.anyActive()) { g_sketch.cancel(); js_hideTextInput(); }
            else g_edit.clearSelection();
        }

        // 뷰 모드 전환 / 표준 뷰
        if (g_cameraController.consumeViewModeToggle()) {
            g_camera.setViewMode(g_camera.isCadMode() ? LotCamera::ViewMode::Fps
                                                      : LotCamera::ViewMode::Cad);
            LOT_LOG("view: " << (g_camera.isCadMode() ? "cad orbit" : "fps"));
        }
        if (const int preset = g_cameraController.consumeViewPreset(); preset >= 0) {
            g_camera.setViewMode(LotCamera::ViewMode::Cad);
            g_camera.resetCadView(static_cast<LotCamera::CadViewType>(preset));
            static const char* kViewNames[] = {"front", "back", "top", "bottom",
                                               "right", "left", "isometric"};
            LOT_LOG("view: " << kViewNames[preset]);
        }

        // 마우스/키로 카메라를 움직인다. 델타와 휠은 모드와 관계없이 매 프레임
        // 비워야 한다 - 안 그러면 모드를 바꾼 순간 쌓인 값이 한꺼번에 들어간다.
        float mouseDx = 0.0f, mouseDy = 0.0f;
        g_mouse.consumeDelta(mouseDx, mouseDy);
        const float wheel = g_mouse.consumeWheel();
        if (g_camera.isCadMode()) {
            // 우클릭 궤도, 중클릭 팬, 휠 줌, 화살표 궤도
            if (g_mouse.isRightDown()) {
                // 부호가 음수인 이유: 팬과 마찬가지로 '장면을 잡고 끄는' 느낌이어야 한다.
                // 오른쪽으로 끌면 장면이 오른쪽으로 돌아야 하므로 카메라는 왼쪽으로 간다.
                g_camera.orbitAroundTarget(-mouseDx * kOrbitRadPerPixel, -mouseDy * kOrbitRadPerPixel);
            } else if (g_mouse.isMiddleDown()) {
                g_camera.panTarget(mouseDx, mouseDy,
                                   static_cast<float>(g_renderer->getSwapchain().getHeight()));
            }
            float yaw = 0.0f, pitch = 0.0f;
            g_cameraController.orbitInput(yaw, pitch);
            if (yaw != 0.0f || pitch != 0.0f) {
                const float step = kOrbitRadPerSec * static_cast<float>(deltaSec);
                g_camera.orbitAroundTarget(yaw * step, pitch * step);
            }
            if (wheel != 0.0f) {
                if (g_orthographic) {
                    g_orthoHalfHeight *= LotCamera::zoomFactor(wheel);
                    if (g_orthoHalfHeight < g_orthoMinHalfHeight) g_orthoHalfHeight = g_orthoMinHalfHeight;
                    if (g_orthoHalfHeight > g_orthoMaxHalfHeight) g_orthoHalfHeight = g_orthoMaxHalfHeight;
                } else {
                    g_camera.zoomToTarget(wheel);
                }
            }
        } else {
            // 1인칭: 키 입력을 뷰어 오브젝트에 반영한 뒤, 그 위치/회전으로 뷰 행렬을 만든다.
            // 첫 프레임은 deltaSec 이 0 이라 아무 일도 일어나지 않는다.
            g_cameraController.moveInPlaneXY(static_cast<float>(deltaSec), g_viewerObject);
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
            g_edit.duplicateSelection(g_gameObjects);
        }
        if (g_cameraController.consumeDelete() && editKeysEnabled) {
            g_edit.deleteSelection(g_gameObjects);
        }
        if (g_cameraController.consumeOutlineToggle()) {
            g_postSystem->mode = (g_postSystem->mode == PostProcessSystem::Mode::Outline)
                ? PostProcessSystem::Mode::Passthrough : PostProcessSystem::Mode::Outline;
            LOT_LOG("post: " << (g_postSystem->mode == PostProcessSystem::Mode::Outline
                                 ? "outline" : "passthrough"));
        }
        if (g_cameraController.consumeProjectionToggle()) {
            g_orthographic = !g_orthographic;
            LOT_LOG("projection: " << (g_orthographic ? "orthographic" : "perspective"));
        }
        if (g_orthographic) {
            // 지수적으로 줄이고 키워야 어느 배율에서든 같은 '느낌'으로 줌된다
            const int zoom = g_cameraController.zoomDirection();
            if (zoom != 0) {
                const float factor = std::exp(-zoom * kOrthoZoomSpeed * static_cast<float>(deltaSec));
                g_orthoHalfHeight *= factor;
                if (g_orthoHalfHeight < g_orthoMinHalfHeight) g_orthoHalfHeight = g_orthoMinHalfHeight;
                if (g_orthoHalfHeight > g_orthoMaxHalfHeight) g_orthoHalfHeight = g_orthoMaxHalfHeight;
            }
        }

        // 메뉴 · 리본 · 레이어 패널 (바뀐 것만 DOM 으로 나간다)
        {
            lot_ui::State ui;
            ui.gizmoMode = static_cast<int>(g_gizmoSystem->mode);
            ui.sketchTool = g_sketch.activeKind();
            ui.xformMode = g_transform.isActive() ? g_transform.modeIndex() - 1 : -1;
            ui.view = g_camera.presetViewIndex();
            ui.fps = !g_camera.isCadMode();
            ui.ortho = g_orthographic;
            ui.orthoTracking = lot_cursor::settings().ortho;
            ui.gridSnap = lot_cursor::settings().gridSpacing > 0.0f;
            ui.outline = g_postSystem->mode == PostProcessSystem::Mode::Outline;
            ui.canUndo = g_edit.history().canUndo();
            ui.canRedo = g_edit.history().canRedo();
            ui.hint = g_transform.isActive() ? g_transform.hint() : g_sketch.hint();
            g_ui.update(ui, g_layers, g_gameObjects, g_edit);
        }

        // 카메라 갱신. 종횡비는 매 프레임 현재 값으로 넣어두면
        // 리사이즈를 따로 챙기지 않아도 항상 맞는다.
        if (g_orthographic) {
            g_camera.setOrthographicProjection(g_orthoHalfHeight, g_renderer->getAspectRatio(),
                                               g_nearZ, g_farZ);
        } else {
            g_camera.setPerspectiveProjection(kFovY, g_renderer->getAspectRatio(), g_nearZ, g_farZ);
        }
        if (g_camera.isCadMode()) {
            g_camera.updateCadView();
        } else {
            g_camera.setViewFromTransform(g_viewerObject.transform.translation,
                                          g_viewerObject.transform.rotation);
        }

        // (예전의 자동 회전은 뺐다 - 회전/축척 기즈모로 편집한 값을 매 프레임
        //  덮어쓰기 때문이다. 초기 자세는 createGameObjects / placeObjModel 에서 준다.)

        // 광원을 큐브들 주위로 돌린다. 점 광원이라 가까운 면일수록 밝아지는 게
        // 눈에 보인다 (방향 광원이었다면 어디에 두든 결과가 같다).
        const float lightAngle = static_cast<float>(g_time) * kLightOrbitSpeed;
        g_lighting.pointLight.position = vec3(std::cos(lightAngle) * kLightSwingX,
                                              kLightBaseY + std::sin(lightAngle) * kLightSwingY,
                                              kLightHeight);

        // 프레임당 유니폼 갱신. 렌더 시스템 전부가 같은 값을 본다.
        g_globalUniform.update(g_camera, g_lighting);

        // 패스 1: 장면을 오프스크린 타깃에. 스왑체인과 같은 크기/포맷으로 맞춘다.
        const auto& sc = g_renderer->getSwapchain();
        g_sceneTarget.ensureSize(device, static_cast<uint32_t>(sc.getWidth()),
                                 static_cast<uint32_t>(sc.getHeight()),
                                 sc.getFormat(), sc.getDepthFormat());
        WGPURenderPassEncoder scenePass = g_sceneTarget.beginRenderPass(
            g_renderer->getCurrentEncoder(), WGPUColor{0.1, 0.1, 0.1, 1.0});

        // 이번 프레임 묶음. 렌더 시스템은 이것 하나만 받는다.
        FrameInfo frame{
            static_cast<float>(deltaSec),
            scenePass,
            g_camera,
            g_globalUniform.getBindGroup(),
            g_gameObjects,
            layerVisible,  // 꺼진 층의 메시는 건너뛴다
            displayColor,  // '층 따름'이면 층 색
        };

        // 이번 프레임의 보조선. 광원 위치를 십자로, 작업 영역을 상자로.
        // 프레임마다 다시 채우므로 광원이 움직이면 십자도 따라간다.
        g_lineSystem->clear();
        // 바닥 격자. 그리드 스냅(F9) 과 같은 간격으로, 카메라를 따라다닌다.
        lot_grid::draw(*g_lineSystem, g_camera, g_gridSpacing, static_cast<float>(sc.getHeight()));
        g_lineSystem->addCross(g_lighting.pointLight.position, 0.12f, vec3(1.0f, 0.95f, 0.6f));
        g_lineSystem->addBox(vec3(-2.4f, -0.9f, 0.0f), vec3(2.4f, 0.9f, 1.5f),
                             vec3(0.45f, 0.45f, 0.5f));

        // 선택 상자 / 박스 선택 사각형 / 스냅 마커
        {
            EditController::Context ctx{g_camera, g_mouse, *g_gizmoSystem, g_gameObjects,
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_edit.drawOverlay(*g_lineSystem, ctx);
        }

        // 스케치 오브젝트 + 치수 + 그리는 중인 프리뷰
        g_polylineSystem->clear();
        g_textSystem->clear();
        for (const auto& entry : g_gameObjects) {
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
                    const float minDash = g_camera.worldPerPixel(obj.transform.translation,
                                                                 static_cast<float>(sc.getHeight())) * 4.0f;
                    lot_linetype::emit(*g_lineSystem, obj.worldPoints(), obj.closed, color, lt,
                                       g_linetypeScale, minDash);
                }
            } else if (obj.isDimension()) {
                lot_dim::draw(obj, g_camera, *g_lineSystem, *g_textSystem, color);
            } else if (obj.isText()) {
                lot_text::draw(obj, *g_textSystem, color);
            }
        }
        {
            SketchController::Context sctx{g_camera, g_mouse, g_gameObjects, g_edit.snap(),
                                           static_cast<float>(sc.getWidth()),
                                           static_cast<float>(sc.getHeight())};
            g_sketch.drawPreview(*g_polylineSystem, *g_lineSystem, *g_textSystem, sctx);
            TransformTool::Context tctx{g_camera, g_mouse, g_gameObjects, g_edit.snap(),
                                        static_cast<float>(sc.getWidth()),
                                        static_cast<float>(sc.getHeight())};
            g_transform.drawOverlay(*g_lineSystem, tctx);
        }

        // 폴리라인 둘: 광원이 도는 궤도(닫힘)와 가운데를 감는 나선(열림).
        // 둘을 draw 한 번에 그리므로 restart 인덱스가 실제로 동작하는지도 보인다.
        {
            std::vector<vec3> orbit;
            for (int i = 0; i < 64; ++i) {
                const float a = 6.2831853f * i / 64.0f;
                orbit.push_back(vec3(std::cos(a) * kLightSwingX,
                                     kLightBaseY + std::sin(a) * kLightSwingY, kLightHeight));
            }
            g_polylineSystem->addPolyline(orbit, vec3(0.9f, 0.8f, 0.3f), /*closed=*/true);

            std::vector<vec3> spiral;
            for (int i = 0; i <= 120; ++i) {
                const float t = i / 120.0f;
                const float a = t * 6.2831853f * 3.0f;
                const float r = 0.95f;
                spiral.push_back(vec3(std::cos(a) * r, std::sin(a) * r, kObjHeight - 0.55f + t * 1.1f));
            }
            g_polylineSystem->addPolyline(spiral, vec3(0.4f, 0.9f, 0.9f));
        }

        // 패스 1 에는 메시만. 격자/보조선/기즈모는 후처리에 걸리면 안 되므로
        // (선 하나하나가 뎁스 불연속이라 전부 외곽선으로 잡힌다) 패스 3 으로 미룬다.
        g_renderSystem->render(frame);

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
        g_edit.drawGizmo(frame, *g_gizmoSystem);
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

    // 카메라: CAD 궤도가 기본. 앞-왼쪽-위에서 내려다보는 3/4 뷰로 시작한다.
    // 1인칭(V) 용 뷰어 오브젝트 위치도 같이 잡아둔다.
    g_camera.setViewFromDirection(normalize(vec3{-0.45f, -1.0f, 0.45f}));  // 앞-왼쪽-위
    g_viewerObject.transform.translation = kCameraStartPosition;
    g_cameraController.init();
    // 마우스는 캔버스가 생긴 뒤에 (렌더 루프 1 단계) 등록한다

    // OBJ 열기 버튼
    js_setupObjFileInput();

    LOT_LOG("Renderer initialized (fullscreen canvas).");

    // 렌더 루프 시작
    emscripten_set_main_loop(renderLoop, 0, 1);

    return 0;
}
