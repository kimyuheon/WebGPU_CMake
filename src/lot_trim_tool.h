#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"

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
