// 초기화 단계 (모델 -> 유니폼 -> 파이프라인 -> 기본 씬) 와 렌더 루프. 나머지는 app/ (app/lot_app.h 참고).
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
#include "lot_trim_tool.h"
#include "lot_fillet_tool.h"
#include "lot_array_tool.h"
#include "lot_edit_ops.h"
#include "lot_stretch_tool.h"
#include "lot_extrude_tool.h"
#include "lot_feature.h"
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

#include "app/lot_app.h"

// 지난 프레임 시각 (경과 시간 계산)
static double g_lastFrameMs = 0.0;

// 초기화 상태
static bool g_modelCreated = false;
static bool g_objRequested = false;   // fetch 를 시작했는지 (한 번만 보낸다)

static bool g_pipelineCreated = false;
static bool g_uniformCreated = false;
static bool g_overlayCreated = false;
static bool g_gameObjectsCreated = false;

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

        handleFrameInput(deltaSec);
        pushUiState();

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

        drawSceneObjects(static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight()));
        drawToolOverlays(static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight()));
        drawFeatureDims(static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight()));

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
