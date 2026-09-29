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
    bool isLeftDown() const { return down_[0]; }
    bool isMiddleDown() const { return down_[1]; }
    bool isRightDown() const { return down_[2]; }

    // 지난 프레임 이후 누적된 이동량 (픽셀). 읽으면 0 으로 돌아간다.
    // 궤도/팬은 위치가 아니라 '얼마나 움직였나'가 필요하다.
    void consumeDelta(float& dx, float& dy);

    // 지난 프레임 이후 누적된 휠 노치 (위로 = +). 읽으면 0 으로 돌아간다.
    float consumeWheel();

    // 마지막으로 눌렀을 때 Shift 가 눌려 있었나. 다중 선택(추가/토글)에 쓴다.
    // 키보드 리스너가 아니라 마우스 이벤트의 modifier 를 본다 - 타이밍이 정확하다.
    bool shiftAtPress() const { return shiftAtPress_; }

    // 이번 프레임에 왼쪽 버튼이 눌렸으면 true. 부르면 플래그가 지워진다.
    bool consumeLeftPress();

    // 방금 누름이 더블 클릭이었나 (같은 자리에서 빠르게 두 번). consumeLeftPress 와
    // 같은 프레임에 묻는다 - 더블 클릭도 누름이므로 평소 선택은 그대로 일어나고,
    // 그 위에 '편집 열기' 같은 동작을 얹는 식이다 (브라우저/CAD 관례).
    bool consumeLeftDoubleClick();
    bool consumeLeftRelease();

    // 브라우저 이벤트 콜백에서만 부른다.
    void onMove(float x, float y);
    void onButton(int button, bool down, float x, float y, bool shift);
    void onWheel(float notches);

    // window 에서 받은 뗌. 좌표가 캔버스 기준이 아니라(페이지 기준) 위치는
    // 건드리지 않고 버튼 상태만 바꾼다. 마지막 mousemove 위치가 그대로 남는다.
    void onButtonReleasedAnywhere(int button);

private:
    // 버튼 인덱스는 브라우저 MouseEvent.button 그대로: 0 왼쪽, 1 가운데, 2 오른쪽.
    static constexpr int kButtons = 3;

    float x_ = 0.0f;
    float y_ = 0.0f;
    float dx_ = 0.0f;  // 프레임 사이 누적 이동
    float dy_ = 0.0f;
    float wheel_ = 0.0f;
    bool down_[kButtons] = {};
    bool leftPressed_ = false;   // 프레임 사이에 왼쪽 눌림이 있었나
    bool leftDoubleClick_ = false;
    double lastPressMs_ = -1000.0;
    float lastPressX_ = 0.0f;
    float lastPressY_ = 0.0f;
    bool leftReleased_ = false;  // 프레임 사이에 왼쪽 뗌이 있었나
    bool shiftAtPress_ = false;
};
