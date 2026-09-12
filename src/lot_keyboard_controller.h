#pragma once

#include "lot_game_object.h"

// 키보드로 카메라(뷰어 오브젝트)를 움직인다.
//
// Vulkan 원본은 glfwGetKey 로 매 프레임 키 상태를 물어봤지만, 브라우저에는
// 그런 폴링 API 가 없다. 그래서 keydown/keyup 이벤트로 눌린 키 집합을
// 직접 들고 있다가 프레임마다 읽는다.
class KeyboardMovementController {
public:
    // 브라우저 키 이벤트 리스너 등록. 한 번만 부르면 된다.
    void init();

    // XZ 평면 위를 걸어다닌다 (비행이 아니라 FPS 이동).
    // 위/아래는 월드 축 그대로라 시선을 위로 들어도 붕 뜨지 않는다.
    void moveInPlaneXZ(float dt, LotGameObject& viewerObject);

    // 브라우저 이벤트 콜백에서만 부른다.
    // 우리가 쓰는 키였으면 true - 그 경우 이벤트를 소비한다.
    // ctrlOrMeta 가 참이면(Ctrl+C 같은 브라우저 단축키) 우리 키로 보지 않는다.
    bool handleBrowserKey(const char* code, bool down, bool ctrlOrMeta = false);

    // 투영 토글 (P). 이번 프레임에 눌렸으면 true - 한 번만 소비된다.
    // 이동 키와 달리 '누르고 있는 동안'이 아니라 '누른 순간'이 의미 있다.
    bool consumeProjectionToggle();

    // 직교 줌 (= 확대, - 축소). 누르고 있는 동안 +1 / -1, 아니면 0.
    int zoomDirection() const;

    // 외곽선 토글 (O). 누른 순간만.
    bool consumeOutlineToggle();

    // 선택 복제 (C) / 삭제 (Delete, Backspace). 누른 순간만.
    bool consumeDuplicate();
    bool consumeDelete();

    float moveSpeed = 3.0f;
    float lookSpeed = 1.5f;

private:
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
        KeyCount,
    };

    // code 문자열을 위 enum 으로. 모르는 키면 KeyCount 를 돌려준다
    // (그 경우 이벤트를 소비하지 않고 브라우저에 넘긴다).
    static KeyId lookupKey(const char* code);

    bool pressed_[KeyCount] = {};
    bool justPressed_[KeyCount] = {};  // 떼었다 누른 순간만 true (키 반복은 무시)
};
