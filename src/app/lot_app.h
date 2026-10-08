#pragma once

// 앱 (main.cpp 와 app/*.cpp) 이 함께 쓰는 상태와 함수. 네이티브 first_app/ 처럼 역할별 파일로 나눴다:
//   lot_app_state.cpp      전역 상태 (렌더러 · 렌더 시스템 · 도구 · 도면 목록 · 표시 설정)
//   lot_app_tools.cpp      도구 묶음 - 열기 전에 내려놓기, 프레임 갱신, Enter/Esc, 안내문, 오버레이
//   lot_app_input.cpp      프레임마다 키 · 마우스 (선택 · 토글 · 실행 취소 · 카메라)
//   lot_app_commands.cpp   '#이름' 명령 · 명령행 · 툴바 키
//   lot_app_documents.cpp  도면 탭 · 전체 보기 · 파일을 탭에 열기
//   lot_app_files.cpp      .lot / .dxf / .obj 열기 · 저장 (JS 에서 오는 것)
//   lot_app_exports.cpp    그 밖의 DOM 콜백 (레이어 · 트리 · 속성 · 문자 · 배열 · 뷰큐브)
//   lot_app_scene.cpp      기본 씬 · 층 콜백 · 객체 그리기
//   main.cpp               초기화 단계와 렌더 루프

#include "gizmo_render_system.h"
#include "line_render_system.h"
#include "lot_array_tool.h"
#include "lot_camera.h"
#include "lot_document.h"
#include "lot_extrude_tool.h"
#include "lot_fillet_tool.h"
#include "lot_game_object.h"
#include "lot_global_uniform.h"
#include "lot_keyboard_controller.h"
#include "lot_lighting.h"
#include "lot_material.h"
#include "lot_math.h"
#include "lot_model.h"
#include "lot_mouse_input.h"
#include "lot_offset_tool.h"
#include "lot_render_target.h"
#include "lot_sketch_tool.h"
#include "lot_stretch_tool.h"
#include "lot_transform_tool.h"
#include "lot_trim_tool.h"
#include "lot_web_renderer.h"
#include "polyline_render_system.h"
#include "post_process_system.h"
#include "simple_render_system.h"
#include "text_render_system.h"
#include "ui/lot_ui.h"

#include <memory>
#include <set>
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
    // 배열 대화상자 (src/js/lot_array_dialog.js). DOM 이 아직이면 0.
    extern int js_uiArrayDialog(const char* json);
}

// ── 렌더러와 렌더 시스템 ──
extern std::unique_ptr<LotWebRenderer> g_renderer;
extern std::unique_ptr<SimpleRenderSystem> g_renderSystem;
extern std::unique_ptr<LineRenderSystem> g_lineSystem;
extern std::unique_ptr<PolylineRenderSystem> g_polylineSystem;
extern std::unique_ptr<GizmoRenderSystem> g_gizmoSystem;
// 장면은 먼저 오프스크린 타깃에 그려지고, 후처리 패스가 그걸 화면에 옮긴다.
extern LotRenderTarget g_sceneTarget;
extern std::unique_ptr<PostProcessSystem> g_postSystem;
extern std::unique_ptr<TextRenderSystem> g_textSystem;
// 카메라 + 조명 유니폼. 렌더 시스템 전부가 이 하나를 @group(0) 으로 본다.
extern LotGlobalUniform g_globalUniform;
extern std::shared_ptr<LotModel> g_cubeModel;
extern std::shared_ptr<LotModel> g_objModel;
// 시험용 체커보드 재질 (토러스). 텍스처 파이프라인과 UV 가 맞는지 눈으로 보는 용도.
extern std::shared_ptr<LotMaterial> g_checkerMaterial;

// ── 입력 · UI · 도구 ──
extern KeyboardMovementController g_cameraController;
extern MouseInput g_mouse;
extern lot_ui::LotUi g_ui;
extern SketchController g_sketch;
extern TransformTool g_transform;
extern OffsetTool g_offset;
extern TrimTool g_trim;
extern FilletTool g_fillet;
extern ArrayTool g_array;
extern BreakTool g_break;
extern StretchTool g_stretch;
extern LengthenTool g_lengthen;
extern ExtrudeTool g_extrude;
// 선택을 기다리는 즉시 명령 ("explode" / "join"). 선택하고 Enter 면 실행 (네이티브 requireSelectionThen).
extern std::string g_pendingOp;
// 더블 클릭으로 연 문자 오브젝트 (없으면 kInvalidId). 입력창이 이 오브젝트를 고친다.
extern LotGameObject::id_t g_editingTextId;

// ── 도면 ──
// 열려 있는 도면들과 지금 보고 있는 것. 탭 하나가 도면 하나다. 도면에만 속하는 것(오브젝트·층·
// 히스토리·시점)은 전부 LotDocument 안에 있어, doc() 가 가리키는 곳만 바꾸면 화면이 통째로 그 도면 것이 된다.
extern std::vector<std::unique_ptr<LotDocument>> g_documents;
extern size_t g_documentIndex;
LotDocument& doc();

// ── 화면 표시 (하단 상태바) ── 도면이 아니라 보는 사람의 설정이라 탭을 바꿔도 그대로다.
struct DisplaySettings {
    bool grid = true;      // F7 바닥 격자
    bool dims = true;      // 치수 표시 (끄면 고를 수도 없다)
    // 비주얼 스타일 (상태바의 셰이딩 메뉴)
    enum Style { Shaded, ShadedEdges, WireMesh, WireEdges, HiddenLine, StyleCount };
    int style = ShadedEdges;
    bool fillFaces() const { return style == Shaded || style == ShadedEdges || style == HiddenLine; }
    bool featureEdges() const { return style == ShadedEdges || style == WireEdges || style == HiddenLine; }
};
extern const char* const kStyleNames[DisplaySettings::StyleCount];
extern DisplaySettings g_display;

// 애니메이션 시간
extern double g_time;

// 조명. 광원은 큐브들 위쪽 앞에 두고 천천히 돌린다 (lot_app_state.cpp).
extern SceneLighting g_lighting;

// ── 상수 ──
// 불러온 모델이 화면에 차는 크기. 남이 만든 OBJ 는 단위가 제각각이라 파일 값을 그대로 쓰면 안 보이거나 덮는다.
inline constexpr float kObjTargetSize = 1.4f;
inline constexpr float kObjHeight = 0.6f;       // 가운데 모델의 중심 높이 (바닥 위)
inline constexpr float kCubeHalf = 0.3f;        // 큐브 반 변 (scale 0.6) - 바닥에 딱 얹히게
inline const vec3 kMeshEdgeColor{0.08f, 0.08f, 0.08f};  // 메시 모서리 선 (거의 검정)
inline constexpr float kFovY = 50.0f * 3.14159265f / 180.0f;
inline const vec3 kCameraStartPosition{0.0f, -2.5f, 0.6f};  // (FPS 모드) 앞(-Y)에서 눈높이로
// CAD 궤도 조작 감도
inline constexpr float kOrbitRadPerPixel = 0.005f;  // 우클릭 드래그 1px 당 (한 바퀴 ≈ 1250px)
inline constexpr float kOrbitRadPerSec = 1.5f;      // 화살표로 돌릴 때
inline constexpr float kOrthoZoomSpeed = 2.0f;      // 직교 줌, 초당 배율
// 광원은 큐브들 '앞쪽'(카메라 쪽)에서 좌우로 오간다 - 같은 깊이에 두면 앞면이 전부 광원을 등진다.
inline constexpr float kLightSwingX = 1.5f;
inline constexpr float kLightHeight = 1.4f;   // Z-up: 바닥 위 1.4
inline constexpr float kLightBaseY = -1.2f;   // 카메라 쪽(-Y)으로 조금
inline constexpr float kLightSwingY = 0.8f;
inline constexpr float kLightOrbitSpeed = 0.8f;

// ── lot_app_scene.cpp ──
void createGameObjects();
void placeObjModel();
void pushLinetypes();
std::shared_ptr<LotModel> buildHatchFill(const lot_hatch::HatchData& h);
bool textWorthDrawing(const LotGameObject& obj, float width, float height);
bool layerVisible(const LotGameObject& obj);
bool layerSelectable(const LotGameObject& obj);
uint32_t displayLinetype(const LotGameObject& obj);
vec3 displayColor(const LotGameObject& obj);
// 객체들 (스케치 · 해치 · 치수 · 문자 · 광원 · 메시 모서리) 을 선 / 폴리선 / 문자 시스템에
void drawSceneObjects(float width, float height);

// ── lot_app_commands.cpp ──
bool runAction(const char* code);
void addCube();
void runPendingOp(const std::string& op);
void cutThroughSelected();

// ── lot_app_documents.cpp ──
std::unique_ptr<LotDocument> makeDocument();
void applyDocumentScale();
void putDownTools();
void activateDocument(size_t index);
void newDocument();
void closeDocument(size_t index);
void stepDocument(int delta);
bool objectBounds(const std::set<LotGameObject::id_t>* only, vec3& lo, vec3& hi);
void zoomExtents();
void zoomToObjects(const std::set<LotGameObject::id_t>& ids);
void openIntoDocument(LotGameObject::Map&& objects, LotLayers&& layers, const char* fileName);

// ── lot_app_files.cpp ──
std::shared_ptr<LotModel> buildFaceMesh(const std::vector<vec3>& tris);

// ── lot_app_tools.cpp ── 도구는 한 번에 하나. 새 도구를 더할 때 고칠 곳은 이 묶음이다.
// 열린 도구를 모두 내려놓는다 (keepSketch: 스케치 도구는 그대로 - 스케치를 바꿔 열 때)
void cancelTools(bool keepSketch = false);
bool toolsTakeClicks();                  // 열린 도구가 왼쪽 클릭을 가져가는가 (편집기는 선택하지 않는다)
const vec3* toolReferencePoint();        // 직교 / 극좌표 / 수직 스냅의 기준점 (없으면 nullptr)
void updateTools(float width, float height);
void enterTools();                       // Enter: 열린 도구를 확정 (없으면 스케치 끝내기)
void escapeTools();                      // Esc: 열린 도구를 닫는다 (없으면 선택 해제)
bool typedToolValue(const std::string& text);   // 명령행의 값 / 도구 옵션. 받았으면 true
std::string toolHint();
void fillToolUiState(lot_ui::State& ui);
void drawToolOverlays(float width, float height);

// ── lot_app_input.cpp ──
// 선택 · 도구 · 토글 · 실행 취소 · Enter/Esc · 카메라 조작 (프레임마다 한 번)
void handleFrameInput(double deltaSec);
// 메뉴 · 리본 · 레이어 패널에 지금 상태 (바뀐 것만 DOM 으로 나간다)
void pushUiState();
