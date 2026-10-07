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

// 필렛 / 모따기 (AutoCAD FILLET / CHAMFER, 네이티브 first_app/modify.cpp 와 같은 흐름).
//   도구 시작 -> 첫 선 클릭 -> 둘째 선 위에서 미리보기 -> 클릭하면 적용 -> 다음 (반복) ... Esc 로 끝
//   반지름(모따기는 거리)은 언제든 숫자 + Enter 로 바꾼다 (선 고르기 전이든 사이든). 값 없이 Enter 는 끝.
//
// 두 선(또는 열린 폴리선의 끝 변): 클릭한 쪽을 남기고 접점까지 자르거나 늘린 뒤, 그 사이에 접하는
// 호(모따기는 비스듬한 선)를 새 객체로 만든다. 반지름 0 이면 교점까지 맞붙인 모서리만.
// 같은 폴리선의 이웃한 두 변(사각형 모서리 등)이면 그 꼭짓점 자리에 호 점들을 끼워 넣는다.
// 계산은 시작 때 카메라의 작업평면에 투영한 2D 로 한다.
class FilletTool {
public:
    enum class Mode { None, Fillet, Chamfer };
    enum class State { Idle, PickFirst, PickSecond };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        float width;
        float height;
    };

    void start(Mode mode, const LotCamera& camera, float defaultValue);
    void cancel();
    void update(const Context& ctx, EditHistory& history);
    // Enter: 숫자를 쳤으면 그 값을 반지름(거리)으로, 아니면 도구를 닫는다.
    void finish();

    void setNumberBuffer(const std::string& s) { number_ = s; }
    bool isActive() const { return mode_ != Mode::None; }
    bool wantsNumber() const { return isActive(); }   // 언제든 숫자를 받는다
    Mode mode() const { return mode_; }
    std::string hint() const;
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    // 클릭으로 고른 변 (월드)
    struct Pick {
        LotGameObject::id_t id = LotGameObject::kInvalidId;
        size_t segment = 0;
        vec3 point{};
    };
    struct Plan {
        bool ok = false;
        std::string why;
        // 바뀌는 객체들의 새 월드 점
        std::vector<std::pair<LotGameObject::id_t, std::vector<vec3>>> modified;
        // 새로 만드는 호 / 모따기 선 (points 가 비면 없음)
        std::vector<vec3> createdPoints;
        LotGameObject::Curve createdCurve;   // kind Arc 이면 호 (center/right/up 월드)
        std::vector<std::pair<vec3, vec3>> preview;
    };
    Plan plan(const Pick& a, const Pick& b, const LotGameObject::Map& objects) const;

private:
    bool pickAt(const Context& ctx, Pick& out) const;

    Mode mode_ = Mode::None;
    State state_ = State::Idle;
    float radius_ = 1.0f;     // 모깎기 반지름
    float distance_ = 1.0f;   // 모따기 거리 (두 선 모두)
    bool radiusSet_ = false, distanceSet_ = false;
    std::string number_;
    vec3 right_{1.0f, 0.0f, 0.0f}, up_{0.0f, 1.0f, 0.0f}, normal_{0.0f, 0.0f, 1.0f};
    Pick first_;
    mutable float lastX_ = -1e9f, lastY_ = -1e9f;
    mutable Plan hover_;
};
