#include "lot_web_renderer.h"
#include "simple_render_system.h"
#include "lot_game_object.h"
#include "lot_web_buffer.h"
#include <emscripten/emscripten.h>
#include <iostream>
#include <memory>
#include <vector>

// 전역 객체들
std::unique_ptr<LotWebRenderer> g_renderer = nullptr;
std::unique_ptr<SimpleRenderSystem> g_renderSystem = nullptr;
std::unique_ptr<lot_web_buffer> g_vertexBuffer = nullptr;
std::vector<LotGameObject> g_gameObjects;

// 애니메이션 시간
static double g_time = 0.0;

// 초기화 상태
static bool g_bufferCreated = false;
static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_gameObjectsCreated = false;

// 게임 오브젝트 생성
void createGameObjects() {
    // 삼각형 게임 오브젝트 생성
    auto triangle = LotGameObject::createGameObject();

    // 색상 설정 (현재 셰이더에서는 버텍스 컬러 사용)
    triangle.color = vec3(1.0f, 0.5f, 0.0f);

    // Transform 설정
    triangle.transform2d.translation = vec2(0.0f, 0.0f);
    triangle.transform2d.scale = vec2(1.0f, 1.0f);
    triangle.transform2d.rotation = 0.0f;

    // 모델 정보 설정
    triangle.vertexCount = 3;
    triangle.modelBufferId = 1;  // lot_web_buffer의 첫 번째 버퍼 ID

    g_gameObjects.push_back(std::move(triangle));

    std::cout << "Game objects created: " << g_gameObjects.size() << std::endl;
}

// 렌더 루프
void renderLoop() {
    if (!g_renderer || !g_renderSystem || !g_vertexBuffer) return;

    // 1. 버퍼 생성 (스왑체인 준비 후)
    if (!g_bufferCreated && g_renderer->getSwapchain().isReady()) {
        float vertices[] = {
            // position (x, y, z)    // color (r, g, b)
            0.0f,  0.5f, 0.0f,       1.0f, 0.0f, 0.0f,  // 상단 (빨강)
           -0.5f, -0.5f, 0.0f,       0.0f, 1.0f, 0.0f,  // 좌하단 (초록)
            0.5f, -0.5f, 0.0f,       0.0f, 0.0f, 1.0f   // 우하단 (파랑)
        };
        g_vertexBuffer->createBuffer(vertices);
        g_bufferCreated = true;
    }

    // 2. 파이프라인 생성 (버퍼 준비 후)
    if (!g_pipelineCreated && g_vertexBuffer->isReady()) {
        g_renderSystem->createPipeline();
        g_pipelineCreated = true;
    }

    // 3. Uniform 버퍼 생성 (파이프라인 준비 후)
    if (!g_uniformCreated && g_renderSystem->isPipelineReady()) {
        g_renderSystem->createUniformBuffer();
        g_uniformCreated = true;
    }

    // 4. 게임 오브젝트 생성 (한 번만)
    if (!g_gameObjectsCreated && g_uniformCreated) {
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
        // 시간 업데이트
        g_time += 0.016;

        // 게임 오브젝트 업데이트
        for (auto& obj : g_gameObjects) {
            obj.transform2d.rotation = static_cast<float>(g_time);
        }

        // 렌더 패스 시작
        g_renderer->beginRenderPass();

        // 게임 오브젝트들 렌더링
        g_renderSystem->renderGameObjects(g_gameObjects);

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

    // Vertex 버퍼 생성 (3개 정점 * 6개 float = 72 bytes)
    g_vertexBuffer = std::make_unique<lot_web_buffer>(BufferType::VERTEX, 72);

    std::cout << "Renderer initialized (fullscreen canvas)." << std::endl;

    // 렌더 루프 시작
    emscripten_set_main_loop(renderLoop, 0, 1);

    return 0;
}
