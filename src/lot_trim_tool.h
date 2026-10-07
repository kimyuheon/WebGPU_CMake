#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"
#include "lot_osnap.h"

#include <string>
#include <utility>
#include <vector>

class LineRenderSystem;
class MouseInput;

// 자르기 / 연장 (AutoCAD TRIM / EXTEND 의 '빠른 모드').
//
// 경계를 따로 고르지 않는다 - 보이는 모든 선 · 폴리선 · 원 · 호가 경계다.
//   자르기: 클릭한 자리를 감싸는 두 교점 사이를 지운다. 교점이 없으면 그 객체를 지운다.
//           원은 호가 되고, 닫힌 폴리선은 열린 폴리선이 된다.
//   연장:   클릭한 쪽 끝을 진행 방향(호는 원을 따라)으로 가장 가까운 경계까지 늘린다.
//   Shift+클릭은 반대 동작 (자르기 중 연장, 연장 중 자르기) - AutoCAD 와 같다.
// 클릭은 반복되고 Esc / Enter 로 끝난다. 커서를 올리면 지울 부분(빨강) / 늘릴 부분(하늘)을 보여준다.
//
// 계산은 시작 때 카메라의 작업평면에 투영한 2D 로 한다 (도면은 평면 위에 있다). 원 · 호는
// 점으로 쪼갠 것이 아니라 원의 식으로 교차를 구한다 (선-원, 원-원).
class TrimTool {
public:
    enum class Mode { None, Trim, Extend };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        float width;
        float height;
    };

    void start(Mode mode, const LotCamera& camera);
    void cancel();
    void update(const Context& ctx, EditHistory& history);

    bool isActive() const { return mode_ != Mode::None; }
    Mode mode() const { return mode_; }
    std::string hint() const;
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    // 한 번의 클릭이 할 일. 미리보기와 실행이 같은 계산을 쓴다.
    struct Piece {
        std::vector<vec3> points;          // 월드
        bool closed = false;
        LotGameObject::Curve curve;        // kind != None 이면 원/호 (center/right/up 월드)
    };
    struct Plan {
        enum class Kind { None, Replace, Delete, Extend } kind = Kind::None;
        LotGameObject::id_t target = LotGameObject::kInvalidId;
        std::vector<Piece> pieces;                      // Replace: 남는 조각들
        Piece extended;                                 // Extend: 늘린 모양 (월드)
        std::vector<std::pair<vec3, vec3>> preview;     // 지울 부분 / 늘릴 부분
        std::string why;                                // None 일 때 이유 (로그)
    };
    Plan plan(const Context& ctx, bool extend) const;

private:
    Mode mode_ = Mode::None;
    vec3 right_{1.0f, 0.0f, 0.0f};   // 작업평면 축 (2D 투영용)
    vec3 up_{0.0f, 1.0f, 0.0f};
    vec3 normal_{0.0f, 0.0f, 1.0f};

    // 미리보기 캐시 - 커서가 움직였을 때만 다시 계산한다 (큰 도면에서 매 프레임은 무겁다)
    mutable float lastX_ = -1e9f, lastY_ = -1e9f;
    mutable Plan hover_;
};

// 끊기 (AutoCAD BREAK, 네이티브 first_app/break.cpp 와 같은 흐름).
//   객체 클릭 = 첫 점 -> 둘째 점 클릭(스냅) 이면 그 사이를 지운다. 명령행 '@' 면 첫 점에서 둘로만 나눈다.
//   선 · 열린 폴리선 · 호는 두 조각, 닫힌 폴리선은 p1 -> p2 구간을 지운 열린 폴리선, 원은 p1 -> p2 (반시계)
//   를 지운 호. 원 · 닫힌 폴리선은 두 점이 필요하다. 한 번 하면 끝난다 (반복하지 않는다).
class BreakTool {
public:
    enum class State { Idle, PickObject, PickSecond };
    using Context = TrimTool::Context;

    void start(const LotCamera& camera);
    void cancel();
    void update(const Context& ctx, const lot_osnap::Snap& snap, EditHistory& history);
    // 명령행에서 '@' (첫 점에서 나누기) / 'f' (첫 점 다시)
    bool typed(const std::string& text, LotGameObject::Map& objects, EditHistory& history);

    bool isActive() const { return state_ != State::Idle; }
    std::string hint() const;
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

private:
    bool apply(float p2, bool single, LotGameObject::Map& objects, EditHistory& history);

    State state_ = State::Idle;
    vec3 right_{1.0f, 0.0f, 0.0f}, up_{0.0f, 1.0f, 0.0f}, normal_{0.0f, 0.0f, 1.0f};
    LotGameObject::id_t target_ = LotGameObject::kInvalidId;
    float p1_ = 0.0f;           // 첫 점의 매개변수 (폴리선: 누적 길이, 원/호: 각)
    vec3 p1World_{};
    mutable std::vector<std::pair<vec3, vec3>> hover_;
    mutable float lastX_ = -1e9f, lastY_ = -1e9f;
};

