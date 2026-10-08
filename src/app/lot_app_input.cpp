// 프레임 입력: 선택 · 기즈모 · 도구 갱신, 토글 키, 실행 취소, Enter/Esc, 카메라 조작. 그리고 UI 상태 밀기.
#include "app/lot_app.h"

#include "lot_edit_controller.h"
#include "lot_osnap.h"
#include "lot_cursor_snap.h"
#include "lot_cursor_snap.h"
#include "lot_log.h"

#include <cstdlib>

void handleFrameInput(double deltaSec) {
    // 선택 / 기즈모 드래그 / 스냅. 카메라가 갱신된 뒤에 해야 레이가 이번 프레임
    // 것과 맞지만, 한 프레임 차이는 눈에 띄지 않으므로 이전 프레임 카메라로 한다.
    {
        const auto& sc = g_renderer->getSwapchain();
        EditController::Context ctx{doc().camera, g_mouse, *g_gizmoSystem, doc().objects,
                                    static_cast<float>(sc.getWidth()),
                                    static_cast<float>(sc.getHeight()),
                                    toolsTakeClicks(),
                                    g_transform.isPreviewing(),
                                    toolReferencePoint(),
                                    g_transform.isActive() ? nullptr : g_sketch.draftPoints()};
        doc().edit.update(ctx);

        // 도구들: 스냅을 쓰므로 편집기 뒤 (app/lot_app_tools.cpp)
        updateTools(static_cast<float>(sc.getWidth()), static_cast<float>(sc.getHeight()));
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
        cancelTools();
        doc().edit.undo(doc().objects);
    }
    if (g_cameraController.consumeRedo()) {
        cancelTools();
        doc().edit.redo(doc().objects);
    }

    // 스케치 도구 시작 / 끝 / 취소. 도구를 열면 선택은 비운다 (Vulkan 쪽과 같다).
    if (const int tool = g_cameraController.consumeSketchTool(); tool >= 0) {
        cancelTools(/*keepSketch=*/true);
        doc().edit.clearSelection();
        g_sketch.start(static_cast<SketchController::Kind>(tool), doc().camera);
    }
    // 변환 도구 (기준점 방식). 선택이 있어야 한다.
    if (const int mode = g_cameraController.consumeTransformMode(); mode >= 0) {
        cancelTools();
        g_transform.start(static_cast<TransformTool::Mode>(mode + 1), doc().edit.selection(),
                          doc().camera, doc().objects);
    }
    if (g_cameraController.consumeEnter()) enterTools();
    if (g_cameraController.consumeEscape()) escapeTools();

    // 뷰 모드 전환 / 표준 뷰
    if (g_cameraController.consumeViewModeToggle()) {
        doc().camera.setViewMode(doc().camera.isCadMode() ? LotCamera::ViewMode::Fps
                                                  : LotCamera::ViewMode::Cad);
        LOT_LOG("view: " << (doc().camera.isCadMode() ? "cad orbit" : "fps"));
    }
    if (const int preset = g_cameraController.consumeViewPreset(); preset >= 0) {
        doc().camera.setViewMode(LotCamera::ViewMode::Cad);
        // 방향만 바꾸고 보던 자리 · 거리는 그대로 (뷰큐브 면과 같다). 예전에는 거리를 장난감 씬의 기본값으로
        // 되돌려서, mm 도면에서 T / I 를 누르면 부품 안으로 파고들어 화면이 비었다.
        doc().camera.setCadViewDirection(static_cast<LotCamera::CadViewType>(preset));
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
}

void pushUiState() {
    // 메뉴 · 리본 · 레이어 패널 (바뀐 것만 DOM 으로 나간다)
    {
        lot_ui::State ui;
        ui.gizmoMode = static_cast<int>(g_gizmoSystem->mode);
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
        fillToolUiState(ui);   // 열린 도구 · 안내문 (app/lot_app_tools.cpp)
        std::vector<lot_ui::DocTab> tabs;
        tabs.reserve(g_documents.size());
        for (const auto& d : g_documents) tabs.push_back({d->name, d->modified()});
        g_ui.update(ui, doc().layers, doc().objects, doc().edit, tabs,
                    static_cast<int>(g_documentIndex));
        g_ui.updateViewCube(doc().camera);
    }
}
