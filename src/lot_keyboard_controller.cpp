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
    return self->handleBrowserKey(e->code, down, e->ctrlKey || e->metaKey, e->shiftKey);
}

bool onKeyDown(int, const EmscriptenKeyboardEvent* e, void* userData) {
    return handleKey(e, userData, true);
}

bool onKeyUp(int, const EmscriptenKeyboardEvent* e, void* userData) {
    return handleKey(e, userData, false);
}

}  // namespace

KeyboardMovementController::KeyId KeyboardMovementController::lookupKey(const char* code) const {
    // A/D 는 모드에 따라 다르다: CAD 궤도에서는 WASD 가 놀고 있으니 호(Arc)/치수(Dimension),
    // FPS 에서는 좌/우 이동.
    if (std::strcmp(code, "KeyA") == 0) return cadMode_ ? SketchArc : MoveLeft;
    if (std::strcmp(code, "KeyD") == 0) return cadMode_ ? SketchDimension : MoveRight;
    if (std::strcmp(code, "KeyW") == 0) return cadMode_ ? SketchText : MoveForward;  // W = write

    struct Entry {
        const char* code;
        KeyId id;
    };
    static const Entry kTable[] = {
        {"KeyS", MoveBackward},
        {"KeyE", MoveUp},       {"KeyQ", MoveDown},
        {"ArrowLeft", LookLeft},{"ArrowRight", LookRight},
        {"ArrowUp", LookUp},    {"ArrowDown", LookDown},
        {"KeyP", ToggleProjection},
        {"Equal", ZoomIn},      {"Minus", ZoomOut},
        {"KeyO", ToggleOutline},
        // G/R/S 는 WASD 의 S 와 겹친다 (S = 후진). 숫자 키로 - 충돌이 없다.
        {"Digit1", GizmoTranslate}, {"Digit2", GizmoRotate}, {"Digit3", GizmoScale},
        {"Delete", DeleteSelection}, {"Backspace", DeleteSelection},
        {"KeyV", ToggleViewMode},
        {"KeyF", ViewFront}, {"KeyT", ViewTop}, {"KeyR", ViewRight}, {"KeyI", ViewIsometric},
        {"KeyL", SketchLine}, {"KeyB", SketchRectangle}, {"KeyN", SketchPolyline},
        {"KeyC", SketchCircle}, {"KeyG", SketchPolygon},
        {"KeyM", XformMove}, {"KeyU", XformCopy}, {"KeyK", XformRotate}, {"KeyX", XformScale},
        {"BracketLeft", PolygonSidesDown}, {"BracketRight", PolygonSidesUp},
        {"Enter", Enter}, {"NumpadEnter", Enter}, {"Escape", Escape},
        {"KeyZ", ZoomExtents},
        {"F8", OrthoTracking}, {"F9", GridSnap},
    };

    for (const auto& entry : kTable) {
        if (std::strcmp(entry.code, code) == 0) {
            return entry.id;
        }
    }
    return KeyCount;
}

bool KeyboardMovementController::handleBrowserKey(const char* code, bool down, bool ctrlOrMeta,
                                                  bool shift) {
    KeyId id = KeyCount;
    if (ctrlOrMeta) {
        // Ctrl 조합은 실행 취소/다시 실행만 우리 것. 나머지(Ctrl+C, Ctrl+R 등)는
        // 브라우저에 넘긴다. Ctrl+Z 는 잡지 않으면 브라우저가 폼 입력을 되돌리려 든다.
        if (std::strcmp(code, "KeyZ") == 0) id = shift ? Redo : Undo;
        else if (std::strcmp(code, "KeyY") == 0) id = Redo;
        else if (std::strcmp(code, "KeyD") == 0) id = Duplicate;  // C 는 원(circle)에 내줬다
        else return false;
    } else {
        if (numberCapture_ && down) {
            // 숫자 버퍼. 여기서 잡힌 키는 아래 표로 가지 않는다 (1/2/3 이 기즈모 모드가 되지 않게).
            const char* digits = "0123456789";
            if (std::strncmp(code, "Digit", 5) == 0 && code[5] && std::strchr(digits, code[5])) {
                number_ += code[5];
                return true;
            }
            if (std::strncmp(code, "Numpad", 6) == 0 && code[6] && !code[7]
                && std::strchr(digits, code[6])) {
                number_ += code[6];
                return true;
            }
            if (std::strcmp(code, "Period") == 0 || std::strcmp(code, "NumpadDecimal") == 0) {
                if (number_.find('.') == std::string::npos) {
                    if (number_.empty() || number_ == "-") number_ += '0';
                    number_ += '.';
                }
                return true;
            }
            if (std::strcmp(code, "Minus") == 0 || std::strcmp(code, "NumpadSubtract") == 0) {
                if (number_.empty()) number_ = "-";
                return true;
            }
            if (std::strcmp(code, "Backspace") == 0) {
                if (!number_.empty()) number_.pop_back();
                return true;
            }
        } else if (numberCapture_ && !down) {
            // 잡은 키의 뗌도 삼킨다 - 아래에서 pressed_ 를 건드리지 않게
            if (std::strncmp(code, "Digit", 5) == 0 || std::strncmp(code, "Numpad", 6) == 0
                || std::strcmp(code, "Period") == 0 || std::strcmp(code, "Minus") == 0
                || std::strcmp(code, "Backspace") == 0) {
                return true;
            }
        }
        id = lookupKey(code);
        if (id == KeyCount) return false;  // 우리 키가 아니면 그대로 넘긴다
    }
    // 키를 누르고 있으면 브라우저가 keydown 을 반복해서 보낸다.
    // '누른 순간'은 떼어져 있던 상태에서 눌릴 때만이다.
    if (down && !pressed_[id]) justPressed_[id] = true;
    pressed_[id] = down;
    return true;  // 화살표로 페이지가 스크롤되지 않도록 이벤트를 소비한다
}

bool KeyboardMovementController::consumeProjectionToggle() {
    const bool was = justPressed_[ToggleProjection];
    justPressed_[ToggleProjection] = false;
    return was;
}

bool KeyboardMovementController::consumeOutlineToggle() {
    const bool was = justPressed_[ToggleOutline];
    justPressed_[ToggleOutline] = false;
    return was;
}

bool KeyboardMovementController::consumeDuplicate() {
    const bool was = justPressed_[Duplicate];
    justPressed_[Duplicate] = false;
    return was;
}

bool KeyboardMovementController::consumeDelete() {
    const bool was = justPressed_[DeleteSelection];
    justPressed_[DeleteSelection] = false;
    return was;
}

int KeyboardMovementController::consumeGizmoMode() {
    const KeyId keys[3] = {GizmoTranslate, GizmoRotate, GizmoScale};
    for (int i = 0; i < 3; ++i) {
        if (justPressed_[keys[i]]) {
            justPressed_[keys[i]] = false;
            return i;
        }
    }
    return -1;
}

void KeyboardMovementController::setNumberCapture(bool on) {
    if (numberCapture_ == on) return;
    numberCapture_ = on;
    if (!on) number_.clear();
}

int KeyboardMovementController::consumeTransformMode() {
    const KeyId keys[4] = {XformMove, XformCopy, XformRotate, XformScale};
    for (int i = 0; i < 4; ++i) {
        if (justPressed_[keys[i]]) {
            justPressed_[keys[i]] = false;
            return i;
        }
    }
    return -1;
}

int KeyboardMovementController::consumePolygonSidesDelta() {
    int delta = 0;
    if (justPressed_[PolygonSidesDown]) { justPressed_[PolygonSidesDown] = false; delta -= 1; }
    if (justPressed_[PolygonSidesUp])   { justPressed_[PolygonSidesUp] = false;   delta += 1; }
    return delta;
}

int KeyboardMovementController::consumeSketchTool() {
    const KeyId keys[8] = {SketchLine, SketchRectangle, SketchPolyline,
                           SketchCircle, SketchArc, SketchPolygon, SketchDimension, SketchText};
    for (int i = 0; i < 8; ++i) {
        if (justPressed_[keys[i]]) {
            justPressed_[keys[i]] = false;
            return i;
        }
    }
    return -1;
}

bool KeyboardMovementController::consumeEnter() {
    const bool was = justPressed_[Enter];
    justPressed_[Enter] = false;
    return was;
}

bool KeyboardMovementController::consumeEscape() {
    const bool was = justPressed_[Escape];
    justPressed_[Escape] = false;
    return was;
}

bool KeyboardMovementController::consumeOrthoToggle() {
    const bool was = justPressed_[OrthoTracking];
    justPressed_[OrthoTracking] = false;
    return was;
}

bool KeyboardMovementController::consumeGridSnapToggle() {
    const bool was = justPressed_[GridSnap];
    justPressed_[GridSnap] = false;
    return was;
}

bool KeyboardMovementController::consumeZoomExtents() {
    const bool was = justPressed_[ZoomExtents];
    justPressed_[ZoomExtents] = false;
    return was;
}

bool KeyboardMovementController::consumeUndo() {
    const bool was = justPressed_[Undo];
    justPressed_[Undo] = false;
    return was;
}

bool KeyboardMovementController::consumeRedo() {
    const bool was = justPressed_[Redo];
    justPressed_[Redo] = false;
    return was;
}

bool KeyboardMovementController::consumeViewModeToggle() {
    const bool was = justPressed_[ToggleViewMode];
    justPressed_[ToggleViewMode] = false;
    return was;
}

int KeyboardMovementController::consumeViewPreset() {
    // LotCamera::CadViewType 순서: Front 0, Back 1, Top 2, Bottom 3, Right 4, Left 5, Isometric 6
    struct { KeyId key; int view; } table[] = {
        {ViewFront, 0}, {ViewTop, 2}, {ViewRight, 4}, {ViewIsometric, 6},
    };
    for (const auto& e : table) {
        if (justPressed_[e.key]) {
            justPressed_[e.key] = false;
            return e.view;
        }
    }
    return -1;
}

void KeyboardMovementController::orbitInput(float& yaw, float& pitch) const {
    yaw = (pressed_[LookRight] ? 1.0f : 0.0f) - (pressed_[LookLeft] ? 1.0f : 0.0f);
    pitch = (pressed_[LookDown] ? 1.0f : 0.0f) - (pressed_[LookUp] ? 1.0f : 0.0f);
}

int KeyboardMovementController::zoomDirection() const {
    return (pressed_[ZoomIn] ? 1 : 0) - (pressed_[ZoomOut] ? 1 : 0);
}

void KeyboardMovementController::init() {
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, onKeyDown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, onKeyUp);
    LOT_LOG("KeyboardMovementController: V view mode (CAD orbit / FPS), "
            "F/T/R/I front/top/right/iso, arrows orbit or look, WASD+QE move (FPS), "
            "P projection, -/= ortho zoom, O outline, Ctrl+D duplicate, Del delete, "
            "1/2/3 gizmo move/rotate/scale, L/B/N/C/A/G/D/W sketch line/rect/polyline/circle/arc/polygon/dimension/text, "
            "[ ] polygon sides, M/U/K/X move/copy/rotate/scale by base point (type value + Enter), "
            "Enter finish, Esc cancel, Ctrl+Z undo, Ctrl+Y redo, Z zoom extents, F8 ortho, F9 grid snap");
}

void KeyboardMovementController::moveInPlaneXY(float dt, LotGameObject& viewerObject) {
    // 1. 시선 회전
    vec3 rotate{0.0f, 0.0f, 0.0f};
    if (pressed_[LookRight]) rotate.y += 1.0f;
    if (pressed_[LookLeft])  rotate.y -= 1.0f;
    if (pressed_[LookUp])    rotate.x += 1.0f;
    if (pressed_[LookDown])  rotate.x -= 1.0f;

    // normalize 해야 대각선(위 + 오른쪽)이 더 빨라지지 않는다.
    if (dot(rotate, rotate) > kEpsilon) {
        const vec3 step = normalize(rotate) * (lookSpeed * dt);
        pitch_ += step.x;
        yaw_ += step.y;
    }

    if (pitch_ < -kMaxPitch) pitch_ = -kMaxPitch;
    if (pitch_ >  kMaxPitch) pitch_ =  kMaxPitch;

    // yaw 는 계속 돌 수 있어야 하므로 자르지 않고 한 바퀴로 접는다
    // (오래 돌렸을 때 float 정밀도가 나빠지는 것을 막는다).
    yaw_ = std::fmod(yaw_, kTwoPi);

    // 1인칭 카메라는 pitch/yaw 두 각으로 충분하고, 이렇게 각을 따로 들고
    // 쿼터니언은 매 프레임 새로 만들면 짐벌락도 누적 오차도 없다.
    // 카메라 기본 자세는 +Y 를 보고 +Z 가 위 (LotCamera::setViewFromTransform 규약).
    // yaw 는 Z 둘레 - 오른쪽으로 돌면 forward 가 (sin, cos, 0) 이 되도록 부호를 맞춘다.
    // pitch 는 (yaw 뒤의) 카메라 X 둘레, 양수가 위.
    viewerObject.transform.rotation = normalize(
        quat::angleAxis(-yaw_, vec3{0.0f, 0.0f, 1.0f}) * quat::angleAxis(pitch_, vec3{1.0f, 0.0f, 0.0f}));
    const float yaw = yaw_;

    // 2. 이동. 시선의 yaw 만 반영하므로 위를 봐도 앞으로만 간다.
    const vec3 forwardDir{std::sin(yaw), std::cos(yaw), 0.0f};
    const vec3 rightDir{forwardDir.y, -forwardDir.x, 0.0f};  // cross(forward, up)
    const vec3 upDir{0.0f, 0.0f, 1.0f};

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
