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
#include <iostream>
#include <memory>
#include <vector>

// 전역 객체들
std::unique_ptr<LotWebRenderer> g_renderer = nullptr;
std::unique_ptr<SimpleRenderSystem> g_renderSystem = nullptr;
std::shared_ptr<LotModel> g_cubeModel = nullptr;
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
    // 큐브 하나를 세 오브젝트가 공유한다 (모델이 shared_ptr 인 이유).
    // 정점 데이터는 GPU 에 한 번만 올라가고, 오브젝트마다 다른 것은 transform 뿐이다.
    const vec3 spawns[] = {
        vec3(-1.2f, 0.0f, 0.0f),
        vec3( 0.0f, 0.0f, 0.0f),
        vec3( 1.2f, 0.0f, 0.0f),
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

// 렌더 루프
void renderLoop() {
    if (!g_renderer || !g_renderSystem) return;

    auto& device = g_renderer->getDevice();

    // 1. 모델 생성 (스왑체인 준비 후)
    if (!g_modelCreated && g_renderer->getSwapchain().isReady()) {
        g_cubeModel = LotModel::createCube(device);
        g_modelCreated = g_cubeModel && g_cubeModel->isReady();
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

    std::cout << "Renderer initialized (fullscreen canvas)." << std::endl;

    // 렌더 루프 시작
    emscripten_set_main_loop(renderLoop, 0, 1);

    return 0;
}
