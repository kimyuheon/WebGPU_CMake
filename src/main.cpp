#include "lot_web_renderer.h"
#include "simple_render_system.h"
#include "lot_game_object.h"
#include "lot_camera.h"
#include "lot_keyboard_controller.h"
#include "lot_lighting.h"
#include "lot_model.h"
#include "lot_math.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdlib>
#include <iostream>
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
std::shared_ptr<LotModel> g_cubeModel = nullptr;
std::shared_ptr<LotModel> g_objModel = nullptr;
std::vector<LotGameObject> g_gameObjects;
LotCamera g_camera;
KeyboardMovementController g_cameraController;

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

// OBJ 모델이 들어가 있는 오브젝트의 자리. 파일을 새로 열면 이 자리를 갈아끼운다.
static size_t g_objObjectIndex = SIZE_MAX;

// 불러온 모델이 화면에 차는 크기. 남이 만든 OBJ 는 단위가 제각각이라
// (몇 백 단위짜리도 흔하다) 파일 값을 그대로 쓰면 안 보이거나 화면을 덮는다.
static const float kObjTargetSize = 1.4f;
static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_gameObjectsCreated = false;

// 카메라 설정
static const float kFovY = 50.0f * 3.14159265f / 180.0f;
static const float kNearZ = 0.1f;
static const float kFarZ = 10.0f;
static const vec3 kCameraStartPosition{0.0f, 0.0f, -2.5f};  // +Z 가 화면 안쪽이라 카메라는 -Z 쪽

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
        cube.transform.translation = translation;
        cube.transform.scale = vec3(0.6f);
        cube.transform.rotation = vec3(0.0f, 0.0f, 0.0f);

        g_gameObjects.push_back(std::move(cube));
    }

    std::cout << "Game objects created: " << g_gameObjects.size() << std::endl;
}

// 받아온 OBJ 모델을 장면 가운데에 놓는다.
//
// fetch 콜백은 렌더 루프 바깥(브라우저 이벤트 루프)에서 불리므로,
// 프레임 도중에 벡터가 바뀔 걱정은 없다.
void placeObjModel() {
    auto object = LotGameObject::createGameObject();
    object.model = g_objModel;
    object.transform.translation = vec3(0.0f, 0.0f, 0.0f);
    object.transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
    g_gameObjects.push_back(std::move(object));

    g_objObjectIndex = g_gameObjects.size() - 1;
    g_objPlaced = true;
    std::cout << "OBJ model placed at scene center" << std::endl;
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
        std::cerr << "OBJ open: renderer is not ready yet" << std::endl;
        return;
    }

    auto model = LotModel::createFromObjText(g_renderer->getDevice(), text, "(opened file)");
    if (!model) {
        return;  // 실패 이유는 파서/모델 쪽에서 이미 출력했다
    }

    // 이전 모델은 shared_ptr 이 마지막으로 놓을 때 정리된다
    g_objModel = std::move(model);

    if (g_objObjectIndex < g_gameObjects.size()) {
        auto& object = g_gameObjects[g_objObjectIndex];
        object.model = g_objModel;
        object.transform.scale = vec3(g_objModel->fitScale(kObjTargetSize));
        std::cout << "OBJ open: replaced the model at scene center" << std::endl;
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
        g_renderSystem->createUniformBuffer(device);
        g_uniformCreated = g_renderSystem->isUniformReady();
    }

    // 3. 파이프라인 생성 (uniform 레이아웃 준비 후)
    if (!g_pipelineCreated && g_uniformCreated) {
        g_renderSystem->createPipeline(device,
                                       g_renderer->getSwapchain().getFormat(),
                                       g_renderer->getSwapchain().getDepthFormat());
        g_pipelineCreated = true;
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

        // 키 입력을 뷰어 오브젝트에 반영한 뒤, 그 위치/회전으로 뷰 행렬을 만든다.
        // 첫 프레임은 deltaSec 이 0 이라 아무 일도 일어나지 않는다.
        g_cameraController.moveInPlaneXZ(static_cast<float>(deltaSec), g_viewerObject);

        // 카메라 갱신. 종횡비는 매 프레임 현재 값으로 넣어두면
        // 리사이즈를 따로 챙기지 않아도 항상 맞는다.
        g_camera.setPerspectiveProjection(kFovY, g_renderer->getAspectRatio(), kNearZ, kFarZ);
        g_camera.setViewYXZ(g_viewerObject.transform.translation,
                            g_viewerObject.transform.rotation);

        // 게임 오브젝트 업데이트 (각자 다른 속도로 돈다).
        // 두 축을 같이 돌려야 정육면체의 여섯 면이 다 보인다.
        for (size_t i = 0; i < g_gameObjects.size(); ++i) {
            const float speed = kSpinSpeeds[i % (sizeof(kSpinSpeeds) / sizeof(kSpinSpeeds[0]))];
            const float angle = static_cast<float>(g_time) * speed;
            g_gameObjects[i].transform.rotation.y = angle;
            g_gameObjects[i].transform.rotation.x = angle * 0.5f;
        }

        // 광원을 큐브들 주위로 돌린다. 점 광원이라 가까운 면일수록 밝아지는 게
        // 눈에 보인다 (방향 광원이었다면 어디에 두든 결과가 같다).
        const float lightAngle = static_cast<float>(g_time) * kLightOrbitSpeed;
        g_lighting.pointLight.position = vec3(std::cos(lightAngle) * kLightSwingX,
                                              kLightHeight,
                                              kLightBaseZ + std::sin(lightAngle) * kLightSwingZ);

        // 렌더 패스 시작
        g_renderer->beginRenderPass();

        // 게임 오브젝트들 렌더링
        g_renderSystem->renderGameObjects(g_renderer->getCurrentRenderPass(),
                                          g_gameObjects, g_camera, g_lighting);

        // 렌더 패스 종료
        g_renderer->endRenderPass();

        // 프레임 종료
        g_renderer->endFrame();
    }
}

// 메인 함수
int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "WebGPU 3D Engine - Perspective Cubes" << std::endl;
    std::cout << "==================================" << std::endl;

    // Renderer 생성 (동적 크기 - 브라우저 창 크기에 맞춤)
    g_renderer = std::make_unique<LotWebRenderer>();
    g_renderer->init();

    // Render System 생성 (Pipeline + Uniform 관리)
    g_renderSystem = std::make_unique<SimpleRenderSystem>("shaders/triangle.wgsl");

    // 카메라 시작 위치 + 키보드 리스너 등록
    g_viewerObject.transform.translation = kCameraStartPosition;
    g_cameraController.init();

    // OBJ 열기 버튼
    js_setupObjFileInput();

    std::cout << "Renderer initialized (fullscreen canvas)." << std::endl;

    // 렌더 루프 시작
    emscripten_set_main_loop(renderLoop, 0, 1);

    return 0;
}
