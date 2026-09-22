#pragma once

#include "lot_game_object.h"

#include <string>

// 키보드로 카메라(뷰어 오브젝트)를 움직인다.
//
// Vulkan 원본은 glfwGetKey 로 매 프레임 키 상태를 물어봤지만, 브라우저에는
// 그런 폴링 API 가 없다. 그래서 keydown/keyup 이벤트로 눌린 키 집합을
// 직접 들고 있다가 프레임마다 읽는다.
class KeyboardMovementController {
public:
    // 브라우저 키 이벤트 리스너 등록. 한 번만 부르면 된다.
    void init();

    // 바닥(XY) 평면 위를 걸어다닌다 (비행이 아니라 FPS 이동).
    // 위/아래는 월드 Z 축 그대로라 시선을 위로 들어도 붕 뜨지 않는다.
    void moveInPlaneXY(float dt, LotGameObject& viewerObject);

    // 브라우저 이벤트 콜백에서만 부른다.
    // 우리가 쓰는 키였으면 true - 그 경우 이벤트를 소비한다.
    // ctrlOrMeta 가 참이면(Ctrl+C 같은 브라우저 단축키) 우리 키로 보지 않는다.
    bool handleBrowserKey(const char* code, bool down, bool ctrlOrMeta = false,
                          bool shift = false);

    // 투영 토글 (P). 이번 프레임에 눌렸으면 true - 한 번만 소비된다.
    // 이동 키와 달리 '누르고 있는 동안'이 아니라 '누른 순간'이 의미 있다.
    bool consumeProjectionToggle();

    // 직교 줌 (= 확대, - 축소). 누르고 있는 동안 +1 / -1, 아니면 0.
    int zoomDirection() const;

    // 외곽선 토글 (O). 누른 순간만.
    bool consumeOutlineToggle();

    // 선택 복제 (Ctrl+D) / 삭제 (Delete, Backspace). 누른 순간만.
    bool consumeDuplicate();
    bool consumeDelete();

    // 기즈모 모드 (1 이동, 2 회전, 3 축척). 눌린 순간의 모드,
    // 없으면 -1. 값은 GizmoRenderSystem::Mode 순서 (0 이동, 1 회전, 2 축척).
    int consumeGizmoMode();

    // 뷰 모드 토글 (V): CAD 궤도 <-> 1인칭. 누른 순간만.
    bool consumeViewModeToggle();

    // 표준 뷰 (F 정면, T 평면, R 우측면, I 등각). 눌린 순간의 뷰,
    // 없으면 -1. 값은 LotCamera::CadViewType 순서.
    int consumeViewPreset();

    // 스케치 도구 (L 선, B 사각형, N 폴리라인, C 원, A 호, G 다각형). 눌린 순간의 도구,
    // 없으면 -1. 값은 SketchController::Kind 순서.
    int consumeSketchTool();

    // 다각형 변 수 ([ -1, ] +1). 눌린 순간의 증감, 없으면 0.
    int consumePolygonSidesDelta();

    // CAD 궤도 모드인가. A 키가 CAD 에서는 호, FPS 에서는 좌이동이라 매 프레임 알려준다.
    void setCadMode(bool cad) { cadMode_ = cad; }

    // 변환 도구 (M 이동, U 복사, K 회전, X 축척). 눌린 순간의 모드, 없으면 -1.
    // 값은 TransformTool::Mode 순서에서 None 을 뺀 것 (0 이동, 1 복사, 2 회전, 3 축척).
    int consumeTransformMode();

    // 숫자 입력 받기. 켜져 있으면 숫자/./-/Backspace 가 단축키가 아니라 버퍼로 간다
    // (변환 도구가 거리·각도·배율을 받을 때). 1/2/3 기즈모 키도 이때는 숫자다.
    void setNumberCapture(bool on);
    const std::string& numberBuffer() const { return number_; }
    void clearNumberBuffer() { number_.clear(); }

    // Enter / Esc. 누른 순간만.
    bool consumeEnter();
    bool consumeEscape();

    // Ctrl+Z / Ctrl+Y (또는 Ctrl+Shift+Z). 누른 순간만.
    bool consumeUndo();
    bool consumeRedo();

    // 전체 보기 (Z, Ctrl 없이). 누른 순간만.
    bool consumeZoomExtents();

    // 직교 트랙킹 토글 (F8). 누른 순간만.
    bool consumeOrthoToggle();

    // CAD 모드에서 화살표로 궤도. 좌/우 = yaw (-1/+1), 위/아래 = pitch (-1/+1).
    // 안 눌렸으면 둘 다 0. 1인칭 모드에서는 moveInPlaneXY 가 같은 키를 시선으로 쓴다.
    void orbitInput(float& yaw, float& pitch) const;

    float moveSpeed = 3.0f;
    float lookSpeed = 1.5f;

private:
    bool cadMode_ = true;
    bool numberCapture_ = false;
    std::string number_;

    // 시선 각 (라디안). 뷰어 오브젝트의 쿼터니언은 이 둘로 매 프레임 만든다.
    float pitch_ = 0.0f;  // 카메라 오른쪽(X) 축 둘레 (위/아래)
    float yaw_ = 0.0f;    // 월드 위(Z) 축 둘레 (좌/우)

    // 브라우저 KeyboardEvent.code 기준 (자판 배열과 무관한 물리 키 위치).
    enum KeyId {
        MoveForward,
        MoveBackward,
        MoveLeft,
        MoveRight,
        MoveUp,
        MoveDown,
        LookLeft,
        LookRight,
        LookUp,
        LookDown,
        ToggleProjection,
        ZoomIn,
        ZoomOut,
        ToggleOutline,
        Duplicate,
        DeleteSelection,
        GizmoTranslate,
        GizmoRotate,
        GizmoScale,
        ToggleViewMode,
        ViewFront,
        ViewTop,
        ViewRight,
        ViewIsometric,
        SketchLine,
        SketchRectangle,
        SketchPolyline,
        SketchCircle,
        SketchArc,
        SketchPolygon,
        SketchDimension,
        PolygonSidesDown,
        PolygonSidesUp,
        XformMove,
        XformCopy,
        XformRotate,
        XformScale,
        Enter,
        Escape,
        Undo,
        Redo,
        ZoomExtents,
        OrthoTracking,
        KeyCount,
    };

    // code 문자열을 위 enum 으로. 모르는 키면 KeyCount 를 돌려준다
    // (그 경우 이벤트를 소비하지 않고 브라우저에 넘긴다).
    KeyId lookupKey(const char* code) const;

    bool pressed_[KeyCount] = {};
    bool justPressed_[KeyCount] = {};  // 떼었다 누른 순간만 true (키 반복은 무시)
};
