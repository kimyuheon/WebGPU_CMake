#include "lot_web_renderer.h"
#include "simple_render_system.h"
#include "lot_game_object.h"
#include "lot_web_buffer.h"
#include "lot_vertex.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <iostream>
#include <memory>
#include <vector>

// 전역 객체들
std::unique_ptr<LotWebRenderer> g_renderer = nullptr;
std::unique_ptr<SimpleRenderSystem> g_renderSystem = nullptr;
std::unique_ptr<lot_web_buffer> g_vertexBuffer = nullptr;
std::vector<LotGameObject> g_gameObjects;

// 삼각형 정점 (레이아웃은 lot_vertex.h 의 Vertex 하나로 정해진다)
static const Vertex kTriangleVertices[] = {
    {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},  // 상단 (빨강)
    {{-0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},  // 좌하단 (초록)
    {{ 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // 우하단 (파랑)
};

// 애니메이션 시간
static double g_time = 0.0;
static double g_lastFrameMs = 0.0;

// 초기화 상태
static bool g_bufferCreated = false;
static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_gameObjectsCreated = false;

// 오브젝트별 회전 속도 (per-object uniform 이 실제로 도는지 눈으로 확인하려고 다르게 준다)
static const float kSpinSpeeds[] = {1.0f, -1.6f, 0.7f};

// 게임 오브젝트 생성
void createGameObjects() {
    struct Spawn {
        vec2 translation;
        float scale;
    };
    // 위치와 크기가 서로 다른 삼각형 3개.
    // 유니폼 슬롯이 하나뿐이던 시절에는 셋 다 같은 자리에 겹쳐 그려졌다.
    const Spawn spawns[] = {
        {vec2(-0.5f,  0.0f), 0.5f},
        {vec2( 0.0f,  0.0f), 0.8f},
        {vec2( 0.5f,  0.0f), 0.5f},
    };

    for (const auto& spawn : spawns) {
        auto triangle = LotGameObject::createGameObject();

        // 색상 설정 (현재 셰이더에서는 버텍스 컬러 사용)
        triangle.color = vec3(1.0f, 0.5f, 0.0f);

        // Transform 설정
        triangle.transform2d.translation = spawn.translation;
        triangle.transform2d.scale = vec2(spawn.scale, spawn.scale);
        triangle.transform2d.rotation = 0.0f;

        // 모델 정보 설정 (버퍼를 직접 가리킨다 - 예전의 매직 넘버 ID 없음)
        triangle.model = g_vertexBuffer.get();
        triangle.vertexCount = 3;

        g_gameObjects.push_back(std::move(triangle));
    }

    std::cout << "Game objects created: " << g_gameObjects.size() << std::endl;
}

// 렌더 루프
void renderLoop() {
    if (!g_renderer || !g_renderSystem || !g_vertexBuffer) return;

    auto& device = g_renderer->getDevice();

    // 1. 버퍼 생성 (스왑체인 준비 후)
    if (!g_bufferCreated && g_renderer->getSwapchain().isReady()) {
        g_vertexBuffer->createBuffer(device, kTriangleVertices);
        g_bufferCreated = true;
    }

    // 2. Uniform 리소스 생성 (버퍼 준비 후).
    //    파이프라인보다 먼저다 - 여기서 나온 바인드 그룹 레이아웃으로 파이프라인을 만든다.
    if (!g_uniformCreated && g_vertexBuffer->isReady()) {
        g_renderSystem->createUniformBuffer(device);
        g_uniformCreated = g_renderSystem->isUniformReady();
    }

    // 3. 파이프라인 생성 (uniform 레이아웃 준비 후)
    if (!g_pipelineCreated && g_uniformCreated) {
        g_renderSystem->createPipeline(device, g_renderer->getSwapchain().getFormat());
        g_pipelineCreated = true;
    }

    // 4. 게임 오브젝트 생성 (한 번만)
    if (!g_gameObjectsCreated && g_renderSystem->isPipelineReady()) {
        createGameObjects();
        g_gameObjectsCreated = true;
    }

    // 5. 리사이즈 처리
    if (g_renderer->wasWindowResized()) {
        // 필요시 여기서 추가 처리 (예: 뷰포트, 프로젝션 행렬 등)
        g_renderer->resetWindowResizedFlag();
    }

    // 6. 렌더링
    if (g_renderer->beginFrame()) {
        // 시간 업데이트 (고정 0.016 대신 실제 경과 시간을 쓴다)
        const double nowMs = emscripten_get_now();
        const double deltaSec = (g_lastFrameMs > 0.0) ? (nowMs - g_lastFrameMs) / 1000.0 : 0.0;
        g_lastFrameMs = nowMs;
        g_time += deltaSec;

        // 게임 오브젝트 업데이트 (각자 다른 속도로 돈다)
        for (size_t i = 0; i < g_gameObjects.size(); ++i) {
            const float speed = kSpinSpeeds[i % (sizeof(kSpinSpeeds) / sizeof(kSpinSpeeds[0]))];
            g_gameObjects[i].transform2d.rotation = static_cast<float>(g_time) * speed;
        }

        // 렌더 패스 시작
        g_renderer->beginRenderPass();

        // 게임 오브젝트들 렌더링
        g_renderSystem->renderGameObjects(g_renderer->getCurrentRenderPass(), g_gameObjects);

        // 렌더 패스 종료
        g_renderer->endRenderPass();

        // 프레임 종료
        g_renderer->endFrame();
    }
}

// 메인 함수
int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "WebGPU 3D Engine - Dynamic Resize" << std::endl;
    std::cout << "==================================" << std::endl;

    // Renderer 생성 (동적 크기 - 브라우저 창 크기에 맞춤)
    g_renderer = std::make_unique<LotWebRenderer>();
    g_renderer->init();

    // Render System 생성 (Pipeline + Uniform 관리)
    g_renderSystem = std::make_unique<SimpleRenderSystem>("shaders/triangle.wgsl");

    // Vertex 버퍼 생성
    g_vertexBuffer = std::make_unique<lot_web_buffer>(BufferType::VERTEX,
                                                      sizeof(kTriangleVertices));

    std::cout << "Renderer initialized (fullscreen canvas)." << std::endl;

    // 렌더 루프 시작
    emscripten_set_main_loop(renderLoop, 0, 1);

    return 0;
}
