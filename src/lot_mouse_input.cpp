#include "lot_mouse_input.h"
#include "lot_log.h"

#include <emscripten/html5.h>

namespace {

// C++ 쪽이 서피스를 만들 때 쓰는 셀렉터와 같아야 한다 (lot_web_swapchain 참고)
constexpr const char* kCanvasSelector = "#webgpu-canvas";

bool onMouseMove(int, const EmscriptenMouseEvent* e, void* userData) {
    auto* self = static_cast<MouseInput*>(userData);
    self->onMove(static_cast<float>(e->targetX), static_cast<float>(e->targetY));
    return false;  // 소비하지 않는다 - 다른 리스너에도 가게 둔다
}

bool onMouseDown(int, const EmscriptenMouseEvent* e, void* userData) {
    auto* self = static_cast<MouseInput*>(userData);
    self->onButton(e->button, true, static_cast<float>(e->targetX),
                   static_cast<float>(e->targetY));
    return true;  // 캔버스 위에서는 텍스트 선택 같은 기본 동작을 막는다
}

bool onMouseUp(int, const EmscriptenMouseEvent* e, void* userData) {
    // window 리스너라 targetX/Y 가 페이지 기준이다. 캔버스 기준 위치로 쓰면
    // 상태바 높이만큼 튄다. 그래서 위치는 넘기지 않는다.
    auto* self = static_cast<MouseInput*>(userData);
    self->onButtonReleasedAnywhere(e->button);
    return true;
}

}  // namespace

void MouseInput::init() {
    // 주의: 캔버스가 이미 DOM 에 있어야 한다. 셀렉터가 아무것도 못 찾으면
    // 등록이 조용히 실패한다 (UNKNOWN_TARGET). 스왑체인이 캔버스를 만든
    // 뒤에 불러야 한다 - main() 시점에는 아직 없다.
    const auto r1 = emscripten_set_mousemove_callback(kCanvasSelector, this, false, onMouseMove);
    const auto r2 = emscripten_set_mousedown_callback(kCanvasSelector, this, false, onMouseDown);
    // mouseup 은 캔버스 밖에서 떼도 받아야 드래그가 안 끼인다
    const auto r3 = emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, onMouseUp);
    if (r1 != EMSCRIPTEN_RESULT_SUCCESS || r2 != EMSCRIPTEN_RESULT_SUCCESS
        || r3 != EMSCRIPTEN_RESULT_SUCCESS) {
        LOT_ERR("MouseInput: listener registration failed (" << r1 << ", " << r2 << ", " << r3
                << ") - is the canvas in the DOM yet?");
        return;
    }
    LOT_LOG("MouseInput: listening on " << kCanvasSelector);
}

bool MouseInput::consumeLeftPress() {
    const bool was = leftPressed_;
    leftPressed_ = false;
    return was;
}

bool MouseInput::consumeLeftRelease() {
    const bool was = leftReleased_;
    leftReleased_ = false;
    return was;
}

void MouseInput::onMove(float x, float y) {
    x_ = x;
    y_ = y;
}

void MouseInput::onButtonReleasedAnywhere(int button) {
    if (button != 0) return;
    if (leftDown_) leftReleased_ = true;
    leftDown_ = false;
}

void MouseInput::onButton(int button, bool down, float x, float y) {
    if (button != 0) return;  // 왼쪽만 본다
    x_ = x;
    y_ = y;
    if (down && !leftDown_) leftPressed_ = true;
    if (!down && leftDown_) leftReleased_ = true;
    leftDown_ = down;
}
