// 앱 전역 상태의 정의 (선언과 설명은 app/lot_app.h).
#include "app/lot_app.h"

std::unique_ptr<LotWebRenderer> g_renderer = nullptr;
std::unique_ptr<SimpleRenderSystem> g_renderSystem = nullptr;
std::unique_ptr<LineRenderSystem> g_lineSystem = nullptr;
std::unique_ptr<PolylineRenderSystem> g_polylineSystem = nullptr;
std::unique_ptr<GizmoRenderSystem> g_gizmoSystem = nullptr;
LotRenderTarget g_sceneTarget;
std::unique_ptr<PostProcessSystem> g_postSystem = nullptr;
std::unique_ptr<TextRenderSystem> g_textSystem = nullptr;
LotGlobalUniform g_globalUniform;
std::shared_ptr<LotModel> g_cubeModel = nullptr;
std::shared_ptr<LotModel> g_objModel = nullptr;
std::shared_ptr<LotMaterial> g_checkerMaterial = nullptr;

KeyboardMovementController g_cameraController;
MouseInput g_mouse;
lot_ui::LotUi g_ui;
SketchController g_sketch;
TransformTool g_transform;
OffsetTool g_offset;
TrimTool g_trim;
FilletTool g_fillet;
ArrayTool g_array;
BreakTool g_break;
StretchTool g_stretch;
LengthenTool g_lengthen;
ExtrudeTool g_extrude;
std::string g_pendingOp;
LotGameObject::id_t g_editingTextId = LotGameObject::kInvalidId;

std::vector<std::unique_ptr<LotDocument>> g_documents;
size_t g_documentIndex = 0;
LotDocument& doc() { return *g_documents[g_documentIndex]; }

const char* const kStyleNames[DisplaySettings::StyleCount] = {
    "shaded", "shaded + edges", "wireframe (mesh)", "wireframe (edges)", "hidden line"};
DisplaySettings g_display;
double g_time = 0.0;

// 감쇠가 거리 제곱에 반비례하므로 세기는 거리의 제곱 규모로 잡아야 한다
// (거리 1.5 면 감쇠가 1/2.25 이라, 세기 4 정도는 되어야 눈에 찬다).
SceneLighting g_lighting = [] {
    SceneLighting lighting;
    lighting.ambientIntensity = 0.15f;   // 네이티브의 최솟값 (평행광이 있어 0.03 이면 그늘이 새까맣다)
    lighting.pointLight.color = vec3(1.0f, 1.0f, 1.0f);
    lighting.pointLight.intensity = 4.0f;
    return lighting;
}();
