#include "lot_web_renderer.h"
#include "simple_render_system.h"
#include "grid_render_system.h"
#include "line_render_system.h"
#include "polyline_render_system.h"
#include "gizmo_render_system.h"
#include "post_process_system.h"
#include "lot_frame_info.h"
#include "lot_render_target.h"
#include "lot_mouse_input.h"
#include "lot_osnap.h"
#include "lot_picking.h"
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
#include <memory>
#include <string>
#include <vector>

extern "C" {
    // 파일 선택창은 사용자 제스처로만 열 수 있어서 JS 쪽에 버튼을 만든다.
    extern void js_setupObjFileInput();
}

// 전역 객체들
std::unique_ptr<LotWebRenderer> g_renderer = nullptr;
std::unique_ptr<SimpleRenderSystem> g_renderSystem = nullptr;
std::unique_ptr<GridRenderSystem> g_gridSystem = nullptr;
std::unique_ptr<LineRenderSystem> g_lineSystem = nullptr;
std::unique_ptr<PolylineRenderSystem> g_polylineSystem = nullptr;
std::unique_ptr<GizmoRenderSystem> g_gizmoSystem = nullptr;

// 장면은 먼저 오프스크린 타깃에 그려지고, 후처리 패스가 그걸 화면에 옮긴다.
// 두 패스 구조라서 색/뎁스를 다음 패스가 읽을 수 있다 (외곽선, 그림자, GPU 피킹).
LotRenderTarget g_sceneTarget;
std::unique_ptr<PostProcessSystem> g_postSystem = nullptr;

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

// 지금 선택된 오브젝트. 기즈모와 선택 상자가 여기에 붙는다.
static LotGameObject::id_t g_selectedId = LotGameObject::kInvalidId;

// 기즈모 드래그 상태.
//
// 누른 순간의 오브젝트 위치를 기준점으로 잡고, 매 프레임 마우스 레이가
// 축 위의 어디를 가리키는지(s)를 구해 그 차이만큼 옮긴다. 기준점을 매
// 프레임 갱신하면 오차가 누적되므로 시작값을 들고 있는다.
struct GizmoDrag {
    bool active = false;
    int handle = -1;           // 0~2 축, 3~5 평면
    float startS = 0.0f;       // (축) 누른 순간 축 위의 파라미터
    vec3 startHit{};           // (평면) 누른 순간 평면 위의 교점
    vec3 startTranslation{};   // 누른 순간 오브젝트 위치 = 축/평면의 기준점
};
static GizmoDrag g_drag;

// 마지막 클릭의 교점. 정밀 피킹이 실제로 표면을 맞추는지 십자로 표시한다.
static lot_pick::Hit g_lastHit;

// 커서 아래 스냅 후보. 매 프레임 갱신되고 마커로 그려진다.
// 드래그 중이면 끌고 있는 오브젝트가 이 점에 붙는다.
static lot_osnap::Snap g_snap;
static const float kSnapRadiusPx = 14.0f;
static const float kSnapMarkerPx = 7.0f;

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
static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_gridCreated = false;
static bool g_gameObjectsCreated = false;

// 카메라 설정
static const float kFovY = 50.0f * 3.14159265f / 180.0f;
static const float kNearZ = 0.1f;
static const float kFarZ = 10.0f;
static const vec3 kCameraStartPosition{0.0f, 0.0f, -2.5f};  // +Z 가 화면 안쪽이라 카메라는 -Z 쪽

// 투영 모드. P 키로 전환한다.
// 직교의 halfHeight 는 화면 세로 절반에 담기는 월드 길이 - 곧 줌이다.
static bool g_orthographic = false;
static float g_orthoHalfHeight = 1.6f;
static const float kOrthoZoomSpeed = 2.0f;      // 초당 배율
static const float kOrthoMinHalfHeight = 0.2f;
static const float kOrthoMaxHalfHeight = 20.0f;

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
static const float kLightHeight = -0.8f;  // +Y 가 아래라 위쪽은 음수
static const float kLightBaseZ = -1.2f;
static const float kLightSwingZ = 0.8f;
static const float kLightOrbitSpeed = 0.8f;

// 오브젝트별 회전 속도 (per-object uniform 이 실제로 도는지 눈으로 확인하려고 다르게 준다)
static const float kSpinSpeeds[] = {1.0f, -1.6f, 0.7f};

// 게임 오브젝트 생성
void createGameObjects() {
    // 큐브 하나를 여러 오브젝트가 공유한다 (모델이 shared_ptr 인 이유).
    // 정점 데이터는 GPU 에 한 번만 올라가고, 오브젝트마다 다른 것은 transform 뿐이다.
    // 가운데는 OBJ 로 불러온 모델 자리로 비워둔다.
    const vec3 spawns[] = {
        vec3(-1.5f, 0.0f, 0.0f),
        vec3( 1.5f, 0.0f, 0.0f),
    };

    for (const auto& translation : spawns) {
        auto cube = LotGameObject::createGameObject();

        cube.model = g_cubeModel;
        cube.material = g_checkerMaterial;
        cube.transform.translation = translation;
        cube.transform.scale = vec3(0.6f);
        cube.transform.rotation = vec3(0.0f, 0.0f, 0.0f);

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
    object.transform.translation = vec3(0.0f, 0.0f, 0.0f);
    object.transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
    g_objObjectId = object.getId();
    g_gameObjects.emplace(g_objObjectId, std::move(object));

    g_objPlaced = true;
    LOT_LOG("OBJ model placed at scene center");
}

// 사용자가 고른 OBJ 파일이 도착했을 때 JS 가 부른다.
//
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

    // 3-1. 격자/선 렌더 시스템. 글로벌 레이아웃만 있으면 되므로 메시 쪽과 독립이다.
    if (!g_gridCreated && g_uniformCreated) {
        const WGPUTextureFormat color = g_renderer->getSwapchain().getFormat();
        const WGPUTextureFormat depth = g_renderer->getSwapchain().getDepthFormat();
        g_gridSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_lineSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_polylineSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_gizmoSystem->create(device, g_globalUniform.getLayout(), color, depth);
        g_postSystem->create(device, color);  // 화면에 그리므로 스왑체인 포맷, 뎁스 없음
        g_gridCreated = true;
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

        // 클릭 -> 피킹. 카메라가 갱신된 뒤에 해야 레이가 이번 프레임 것과 맞지만,
        // 한 프레임 차이는 눈에 띄지 않으므로 여기서 이전 프레임 카메라로 한다.
        {
            const auto& sc = g_renderer->getSwapchain();
            auto mouseRay = [&]() {
                return lot_pick::screenToRay(
                    g_camera, g_mouse.x(), g_mouse.y(),
                    static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight()));
            };

            // 커서 아래 스냅 후보. 드래그 중이면 끌고 있는 오브젝트는 뺀다
            // (제 정점에 붙으면 안 된다).
            {
                lot_osnap::Query query;
                query.camera = &g_camera;
                query.mouseX = g_mouse.x();
                query.mouseY = g_mouse.y();
                query.width = static_cast<float>(sc.getWidth());
                query.height = static_cast<float>(sc.getHeight());
                query.radiusPx = kSnapRadiusPx;
                query.excludeId = g_drag.active ? g_selectedId : LotGameObject::kInvalidId;
                g_snap = lot_osnap::find(query, mouseRay(), g_gameObjects);
            }

            if (g_mouse.consumeLeftPress()) {
                const lot_pick::Ray ray = mouseRay();

                // 1. 선택된 오브젝트의 기즈모 축을 집었나? 그러면 드래그 시작.
                //    오브젝트 피킹보다 먼저 봐야 한다 - 기즈모는 오브젝트 위에 겹쳐 있다.
                auto* selected = LotGameObject::find(g_gameObjects, g_selectedId);
                const int handle = selected
                    ? g_gizmoSystem->hitTest(ray, g_camera, selected->transform.translation)
                    : -1;
                if (handle >= 0) {
                    const vec3& origin = selected->transform.translation;
                    bool ok = false;
                    if (GizmoRenderSystem::isPlaneHandle(handle)) {
                        ok = GizmoRenderSystem::intersectPlane(
                            ray, origin, GizmoRenderSystem::planeNormalAxis(handle), g_drag.startHit);
                    } else {
                        ok = lot_pick::closestPointOnLine(
                            ray, origin, GizmoRenderSystem::axisDirection(handle), g_drag.startS);
                    }
                    if (ok) {
                        g_drag.active = true;
                        g_drag.handle = handle;
                        g_drag.startTranslation = origin;
                        LOT_LOG("drag: start on " << (GizmoRenderSystem::isPlaneHandle(handle)
                                                      ? "plane " : "axis ") << handle);
                    }
                } else {
                    // 2. 아니면 오브젝트 피킹 - 삼각형 단위. 경계 상자만 보던 예전에는
                    //    토러스의 구멍을 클릭해도 잡혔다.
                    g_lastHit = lot_pick::pickObjectPrecise(ray, g_gameObjects);
                    const auto hit = g_lastHit.id;
                    if (hit != g_selectedId) {
                        g_selectedId = hit;
                    }
                    if (hit == LotGameObject::kInvalidId) {
                        LOT_LOG("pick: nothing (deselected)");
                    } else {
                        LOT_LOG("pick: object " << hit << " tri " << g_lastHit.triangle
                                << " t=" << g_lastHit.t << " at (" << g_lastHit.point.x << ", "
                                << g_lastHit.point.y << ", " << g_lastHit.point.z << ")");
                    }
                }
            }

            // 드래그 중: 마우스 레이가 축 위의 어디를 가리키는지로 위치를 정한다.
            // 축의 기준점은 누른 순간의 위치다 - 움직이는 오브젝트를 기준으로 하면
            // 매 프레임 기준이 밀려 오차가 쌓인다.
            if (g_drag.active) {
                auto* selected = LotGameObject::find(g_gameObjects, g_selectedId);
                if (!selected || !g_mouse.isLeftDown()) {
                    g_drag.active = false;
                    if (selected) {
                        if (g_snap.valid()) {
                            LOT_LOG("snap: " << (g_snap.kind == lot_osnap::Kind::Endpoint
                                                 ? "endpoint" : "midpoint")
                                    << " of object " << g_snap.id << " ("
                                    << g_snap.screenDistance << "px)");
                        }
                        LOT_LOG("drag: end at (" << selected->transform.translation.x << ", "
                                << selected->transform.translation.y << ", "
                                << selected->transform.translation.z << ")");
                    }
                } else if (g_snap.valid()) {
                    // 스냅: 오브젝트 원점을 스냅 점에 맞춘다. 단, 축/평면 구속은 지킨다 -
                    // 축 드래그면 스냅 점을 축에 투영하고, 평면이면 평면에 투영한다.
                    const vec3 target = g_snap.point - g_drag.startTranslation;
                    if (GizmoRenderSystem::isPlaneHandle(g_drag.handle)) {
                        const vec3 n = GizmoRenderSystem::axisDirection(
                            GizmoRenderSystem::planeNormalAxis(g_drag.handle));
                        selected->transform.translation =
                            g_drag.startTranslation + (target - n * dot(target, n));
                    } else {
                        const vec3 axisDir = GizmoRenderSystem::axisDirection(g_drag.handle);
                        selected->transform.translation =
                            g_drag.startTranslation + axisDir * dot(target, axisDir);
                    }
                } else if (GizmoRenderSystem::isPlaneHandle(g_drag.handle)) {
                    // 평면: 지금 교점과 시작 교점의 차이만큼. 둘 다 평면 위라 차이도 평면 위다.
                    vec3 hit;
                    if (GizmoRenderSystem::intersectPlane(
                            mouseRay(), g_drag.startTranslation,
                            GizmoRenderSystem::planeNormalAxis(g_drag.handle), hit)) {
                        selected->transform.translation =
                            g_drag.startTranslation + (hit - g_drag.startHit);
                    }
                } else {
                    // 축: 마우스 레이가 축 위의 어디를 가리키는지로
                    const vec3 axisDir = GizmoRenderSystem::axisDirection(g_drag.handle);
                    float s = 0.0f;
                    if (lot_pick::closestPointOnLine(mouseRay(), g_drag.startTranslation,
                                                     axisDir, s)) {
                        selected->transform.translation =
                            g_drag.startTranslation + axisDir * (s - g_drag.startS);
                    }
                }
            }
            g_mouse.consumeLeftRelease();  // 위에서 isLeftDown 으로 봤으므로 플래그만 비운다
        }

        // 키 입력을 뷰어 오브젝트에 반영한 뒤, 그 위치/회전으로 뷰 행렬을 만든다.
        // 첫 프레임은 deltaSec 이 0 이라 아무 일도 일어나지 않는다.
        g_cameraController.moveInPlaneXZ(static_cast<float>(deltaSec), g_viewerObject);

        // 투영 전환 / 직교 줌
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
                if (g_orthoHalfHeight < kOrthoMinHalfHeight) g_orthoHalfHeight = kOrthoMinHalfHeight;
                if (g_orthoHalfHeight > kOrthoMaxHalfHeight) g_orthoHalfHeight = kOrthoMaxHalfHeight;
            }
        }

        // 카메라 갱신. 종횡비는 매 프레임 현재 값으로 넣어두면
        // 리사이즈를 따로 챙기지 않아도 항상 맞는다.
        if (g_orthographic) {
            g_camera.setOrthographicProjection(g_orthoHalfHeight, g_renderer->getAspectRatio(),
                                               kNearZ, kFarZ);
        } else {
            g_camera.setPerspectiveProjection(kFovY, g_renderer->getAspectRatio(), kNearZ, kFarZ);
        }
        g_camera.setViewYXZ(g_viewerObject.transform.translation,
                            g_viewerObject.transform.rotation);

        // 게임 오브젝트 업데이트 (각자 다른 속도로 돈다).
        // 두 축을 같이 돌려야 정육면체의 여섯 면이 다 보인다.
        for (auto& entry : g_gameObjects) {
            LotGameObject& obj = entry.second;
            const float speed = kSpinSpeeds[obj.getId() % (sizeof(kSpinSpeeds) / sizeof(kSpinSpeeds[0]))];
            const float angle = static_cast<float>(g_time) * speed;
            obj.transform.rotation.y = angle;
            obj.transform.rotation.x = angle * 0.5f;
        }

        // 광원을 큐브들 주위로 돌린다. 점 광원이라 가까운 면일수록 밝아지는 게
        // 눈에 보인다 (방향 광원이었다면 어디에 두든 결과가 같다).
        const float lightAngle = static_cast<float>(g_time) * kLightOrbitSpeed;
        g_lighting.pointLight.position = vec3(std::cos(lightAngle) * kLightSwingX,
                                              kLightHeight,
                                              kLightBaseZ + std::sin(lightAngle) * kLightSwingZ);

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
        };

        // 이번 프레임의 보조선. 광원 위치를 십자로, 작업 영역을 상자로.
        // 프레임마다 다시 채우므로 광원이 움직이면 십자도 따라간다.
        g_lineSystem->clear();
        g_lineSystem->addCross(g_lighting.pointLight.position, 0.12f, vec3(1.0f, 0.95f, 0.6f));
        g_lineSystem->addBox(vec3(-2.4f, -0.9f, -0.9f), vec3(2.4f, 0.9f, 0.9f),
                             vec3(0.45f, 0.45f, 0.5f));

        // 스냅 마커 (끝점 = 사각형, 중점 = 삼각형). 화면 크기가 일정하다.
        lot_osnap::addMarker(*g_lineSystem, g_snap, g_camera,
                             static_cast<float>(sc.getHeight()), kSnapMarkerPx);

        // 선택된 오브젝트에는 경계 상자를 씌운다. 오브젝트 변환을 그대로 타므로
        // 회전하면 상자도 같이 돈다 - 피킹이 보는 것과 정확히 같은 상자다.
        if (auto* selected = LotGameObject::find(g_gameObjects, g_selectedId)) {
            if (selected->model) {
                g_lineSystem->addTransformedBox(selected->model->boundsMin(),
                                                selected->model->boundsMax(),
                                                selected->transform.mat4Transform(),
                                                vec3(1.0f, 0.85f, 0.2f));
            }
        }

        // 폴리라인 둘: 광원이 도는 궤도(닫힘)와 가운데를 감는 나선(열림).
        // 둘을 draw 한 번에 그리므로 restart 인덱스가 실제로 동작하는지도 보인다.
        g_polylineSystem->clear();
        {
            std::vector<vec3> orbit;
            for (int i = 0; i < 64; ++i) {
                const float a = 6.2831853f * i / 64.0f;
                orbit.push_back(vec3(std::cos(a) * kLightSwingX, kLightHeight,
                                     kLightBaseZ + std::sin(a) * kLightSwingZ));
            }
            g_polylineSystem->addPolyline(orbit, vec3(0.9f, 0.8f, 0.3f), /*closed=*/true);

            std::vector<vec3> spiral;
            for (int i = 0; i <= 120; ++i) {
                const float t = i / 120.0f;
                const float a = t * 6.2831853f * 3.0f;
                const float r = 0.95f;
                spiral.push_back(vec3(std::cos(a) * r, 0.55f - t * 1.1f, std::sin(a) * r));
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
        g_gridSystem->render(frame);
        g_lineSystem->render(frame);
        g_polylineSystem->render(frame);
        // 기즈모는 뎁스를 무시하므로 맨 마지막. 선택된 오브젝트에만 붙는다.
        if (auto* selected = LotGameObject::find(g_gameObjects, g_selectedId)) {
            g_gizmoSystem->render(frame, selected->transform.translation,
                                  g_drag.active ? g_drag.handle : -1);
        }
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
    g_gridSystem = std::make_unique<GridRenderSystem>();
    g_lineSystem = std::make_unique<LineRenderSystem>();
    g_polylineSystem = std::make_unique<PolylineRenderSystem>();
    g_gizmoSystem = std::make_unique<GizmoRenderSystem>();
    g_postSystem = std::make_unique<PostProcessSystem>();

    // 카메라 시작 위치 + 키보드 리스너 등록
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
