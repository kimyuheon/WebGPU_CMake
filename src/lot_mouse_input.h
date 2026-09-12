#pragma once

// 캔버스 위의 마우스 상태.
//
// 키보드와 같은 방식이다: 브라우저 이벤트로 상태를 갱신해두고, 렌더 루프가
// 프레임마다 읽는다. 눌림/뗌은 '이번 프레임에 일어났는지'로 한 번만 소비한다
// (이벤트가 프레임 사이에 여러 번 와도 프레임당 한 번으로 본다).
//
// 좌표는 캔버스 왼쪽 위가 원점인 CSS 픽셀이다. 캔버스 백버퍼가 CSS 크기와
// 같게 잡혀 있으므로 (js_getWindowWidth) 그대로 픽셀로 쓰면 된다.
class MouseInput {
public:
    // 캔버스에 리스너 등록. 한 번만 부르면 된다.
    void init();

    float x() const { return x_; }
    float y() const { return y_; }
    bool isLeftDown() const { return leftDown_; }

    // 마지막으로 눌렀을 때 Shift 가 눌려 있었나. 다중 선택(추가/토글)에 쓴다.
    // 키보드 리스너가 아니라 마우스 이벤트의 modifier 를 본다 - 타이밍이 정확하다.
    bool shiftAtPress() const { return shiftAtPress_; }

    // 이번 프레임에 왼쪽 버튼이 눌렸으면 true. 부르면 플래그가 지워진다.
    bool consumeLeftPress();
    bool consumeLeftRelease();

    // 브라우저 이벤트 콜백에서만 부른다.
    void onMove(float x, float y);
    void onButton(int button, bool down, float x, float y, bool shift);

    // window 에서 받은 뗌. 좌표가 캔버스 기준이 아니라(페이지 기준) 위치는
    // 건드리지 않고 버튼 상태만 바꾼다. 마지막 mousemove 위치가 그대로 남는다.
    void onButtonReleasedAnywhere(int button);

private:
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool leftDown_ = false;
    bool leftPressed_ = false;   // 프레임 사이에 눌림이 있었나
    bool leftReleased_ = false;  // 프레임 사이에 뗌이 있었나
    bool shiftAtPress_ = false;
};
