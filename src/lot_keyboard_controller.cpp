#include "lot_keyboard_controller.h"
#include "lot_math.h"
#include "lot_log.h"

#include <emscripten/html5.h>
#include <cmath>
#include <cstring>

namespace {

// 부동소수 비교용. 키를 하나도 안 눌렀을 때 normalize(0,0,0) 을 피하려는 것.
constexpr float kEpsilon = 1e-6f;

// 시선을 위아래로 90도 조금 못 미치게 제한한다.
// 넘어가면 카메라가 뒤집혀서 조작이 이상해진다.
constexpr float kMaxPitch = 1.5f;

constexpr float kTwoPi = 6.28318530718f;

// 두 콜백이 하는 일이 눌림/떼임 한 글자 차이라 한 곳에 모았다.
// 반환값 true 는 '이벤트를 소비했다'는 뜻이다 (em_key_callback_func 규약).
bool handleKey(const EmscriptenKeyboardEvent* e, void* userData, bool down) {
    auto* self = static_cast<KeyboardMovementController*>(userData);
    return self->handleBrowserKey(e->code, down);
}

bool onKeyDown(int, const EmscriptenKeyboardEvent* e, void* userData) {
    return handleKey(e, userData, true);
}

bool onKeyUp(int, const EmscriptenKeyboardEvent* e, void* userData) {
    return handleKey(e, userData, false);
}

}  // namespace

KeyboardMovementController::KeyId KeyboardMovementController::lookupKey(const char* code) {
    struct Entry {
        const char* code;
        KeyId id;
    };
    static const Entry kTable[] = {
        {"KeyW", MoveForward},  {"KeyS", MoveBackward},
        {"KeyA", MoveLeft},     {"KeyD", MoveRight},
        {"KeyE", MoveUp},       {"KeyQ", MoveDown},
        {"ArrowLeft", LookLeft},{"ArrowRight", LookRight},
        {"ArrowUp", LookUp},    {"ArrowDown", LookDown},
    };

    for (const auto& entry : kTable) {
        if (std::strcmp(entry.code, code) == 0) {
            return entry.id;
        }
    }
    return KeyCount;
}

bool KeyboardMovementController::handleBrowserKey(const char* code, bool down) {
    const KeyId id = lookupKey(code);
    if (id == KeyCount) {
        return false;  // 우리 키가 아니면 브라우저에 그대로 넘긴다
    }
    pressed_[id] = down;
    return true;  // 화살표로 페이지가 스크롤되지 않도록 이벤트를 소비한다
}

void KeyboardMovementController::init() {
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, onKeyDown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, onKeyUp);
    LOT_LOG("KeyboardMovementController: WASD move, QE up/down, arrows look");
}

void KeyboardMovementController::moveInPlaneXZ(float dt, LotGameObject& viewerObject) {
    // 1. 시선 회전
    vec3 rotate{0.0f, 0.0f, 0.0f};
    if (pressed_[LookRight]) rotate.y += 1.0f;
    if (pressed_[LookLeft])  rotate.y -= 1.0f;
    if (pressed_[LookUp])    rotate.x += 1.0f;
    if (pressed_[LookDown])  rotate.x -= 1.0f;

    // normalize 해야 대각선(위 + 오른쪽)이 더 빨라지지 않는다.
    if (dot(rotate, rotate) > kEpsilon) {
        viewerObject.transform.rotation =
            viewerObject.transform.rotation + normalize(rotate) * (lookSpeed * dt);
    }

    float& pitch = viewerObject.transform.rotation.x;
    if (pitch < -kMaxPitch) pitch = -kMaxPitch;
    if (pitch >  kMaxPitch) pitch =  kMaxPitch;

    // yaw 는 계속 돌 수 있어야 하므로 자르지 않고 한 바퀴로 접는다
    // (오래 돌렸을 때 float 정밀도가 나빠지는 것을 막는다).
    float& yaw = viewerObject.transform.rotation.y;
    yaw = std::fmod(yaw, kTwoPi);

    // 2. 이동. 시선의 yaw 만 반영하므로 위를 봐도 앞으로만 간다.
    const vec3 forwardDir{std::sin(yaw), 0.0f, std::cos(yaw)};
    const vec3 rightDir{forwardDir.z, 0.0f, -forwardDir.x};
    const vec3 upDir{0.0f, -1.0f, 0.0f};  // +Y 가 아래라 위는 -Y

    vec3 moveDir{0.0f, 0.0f, 0.0f};
    if (pressed_[MoveForward])  moveDir = moveDir + forwardDir;
    if (pressed_[MoveBackward]) moveDir = moveDir - forwardDir;
    if (pressed_[MoveRight])    moveDir = moveDir + rightDir;
    if (pressed_[MoveLeft])     moveDir = moveDir - rightDir;
    if (pressed_[MoveUp])       moveDir = moveDir + upDir;
    if (pressed_[MoveDown])     moveDir = moveDir - upDir;

    if (dot(moveDir, moveDir) > kEpsilon) {
        viewerObject.transform.translation =
            viewerObject.transform.translation + normalize(moveDir) * (moveSpeed * dt);
    }
}
