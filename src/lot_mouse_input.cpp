#include "lot_mouse_input.h"
#include "lot_log.h"

#include <emscripten/emscripten.h>  // emscripten_get_now
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
                   static_cast<float>(e->targetY), e->shiftKey);
    return true;  // 캔버스 위에서는 텍스트 선택 같은 기본 동작을 막는다
}

bool onMouseUp(int, const EmscriptenMouseEvent* e, void* userData) {
    // window 리스너라 targetX/Y 가 페이지 기준이다. 캔버스 기준 위치로 쓰면
    // 상태바 높이만큼 튄다. 그래서 위치는 넘기지 않는다.
    auto* self = static_cast<MouseInput*>(userData);
    self->onButtonReleasedAnywhere(e->button);
    return true;
}

bool onMouseWheel(int, const EmscriptenWheelEvent* e, void* userData) {
    // deltaMode 에 따라 단위가 다르다: 0 픽셀(대개 100/노치), 1 줄(3/노치), 2 페이지.
    // 브라우저 규약은 아래로 스크롤이 +deltaY 이므로 부호를 뒤집어 '위 = 줌 인'으로.
    float perNotch = 100.0f;
    if (e->deltaMode == DOM_DELTA_LINE) perNotch = 3.0f;
    else if (e->deltaMode == DOM_DELTA_PAGE) perNotch = 1.0f;
    auto* self = static_cast<MouseInput*>(userData);
    self->onWheel(static_cast<float>(-e->deltaY) / perNotch);
    return true;  // 페이지가 같이 스크롤되지 않도록
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
    const auto r4 = emscripten_set_wheel_callback(kCanvasSelector, this, false, onMouseWheel);
    if (r1 != EMSCRIPTEN_RESULT_SUCCESS || r2 != EMSCRIPTEN_RESULT_SUCCESS
        || r3 != EMSCRIPTEN_RESULT_SUCCESS || r4 != EMSCRIPTEN_RESULT_SUCCESS) {
        LOT_ERR("MouseInput: listener registration failed (" << r1 << ", " << r2 << ", " << r3
                << ", " << r4 << ") - is the canvas in the DOM yet?");
        return;
    }

    // 우클릭 궤도 중에 브라우저 컨텍스트 메뉴가 뜨면 안 된다. html5.h 에는
    // contextmenu 콜백이 없어서 JS 로 직접 막는다. (가운데 버튼 자동 스크롤은
    // mousedown 을 소비하는 것으로 이미 막힌다.)
    EM_ASM({
        var c = document.querySelector(UTF8ToString($0));
        if (c) c.addEventListener('contextmenu', function(e) { e.preventDefault(); });
    }, kCanvasSelector);

    LOT_LOG("MouseInput: listening on " << kCanvasSelector
            << " (left pick, right orbit, middle pan, wheel zoom)");
}

void MouseInput::consumeDelta(float& dx, float& dy) {
    dx = dx_;
    dy = dy_;
    dx_ = 0.0f;
    dy_ = 0.0f;
}

float MouseInput::consumeWheel() {
    const float w = wheel_;
    wheel_ = 0.0f;
    return w;
}

void MouseInput::onWheel(float notches) {
    wheel_ += notches;
}

bool MouseInput::consumeLeftDoubleClick() {
    const bool was = leftDoubleClick_;
    leftDoubleClick_ = false;
    return was;
}

bool MouseInput::consumeMiddleDoubleClick() {
    const bool was = middleDoubleClick_;
    middleDoubleClick_ = false;
    return was;
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
    dx_ += x - x_;
    dy_ += y - y_;
    x_ = x;
    y_ = y;
}

void MouseInput::onButtonReleasedAnywhere(int button) {
    if (button < 0 || button >= kButtons) return;
    if (button == 0 && down_[0]) leftReleased_ = true;
    down_[button] = false;
}

void MouseInput::onButton(int button, bool down, float x, float y, bool shift) {
    if (button < 0 || button >= kButtons) return;
    // 위치는 갱신하되 델타에는 넣지 않는다 - 누른 자리에서 드래그가 시작되므로
    // 직전 mousemove 와의 차이는 이동이 아니다.
    x_ = x;
    y_ = y;
    if (button == 0) {
        if (down && !down_[0]) {
            leftPressed_ = true;
            shiftAtPress_ = shift;
            // 더블 클릭: 400ms 안에 5px 안에서 두 번. 브라우저의 dblclick 이벤트를 쓰면
            // 캔버스 위에서 선택/드래그와 순서가 꼬이므로 여기서 직접 센다.
            const double now = emscripten_get_now();
            const float dx = x - lastPressX_, dy = y - lastPressY_;
            leftDoubleClick_ = (now - lastPressMs_ < 400.0) && (dx * dx + dy * dy < 25.0f);
            lastPressMs_ = leftDoubleClick_ ? -1000.0 : now;  // 세 번째 클릭은 새로 시작
            lastPressX_ = x;
            lastPressY_ = y;
        }
        if (!down && down_[0]) leftReleased_ = true;
    }
    if (button == 1 && down && !down_[1]) {
        // 가운데 버튼은 끌면 팬이라, 두 번 누름 사이에 크게 움직이지 않았을 때만
        const double now = emscripten_get_now();
        if (now - lastMiddleMs_ < 400.0) {
            middleDoubleClick_ = true;
            lastMiddleMs_ = -1000.0;
        } else {
            lastMiddleMs_ = now;
        }
    }
    down_[button] = down;
}
