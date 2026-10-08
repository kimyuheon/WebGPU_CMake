#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"
#include "lot_osnap.h"

#include <set>
#include <string>
#include <vector>

class LineRenderSystem;
class MouseInput;
class lot_web_device;

// 돌출 (네이티브 ExtrudeManager 흐름) 과 같은 손놀림의 보스 / 포켓.
//   돌출: 닫힌 스케치를 고른다 (선택이 없으면 하나 클릭) -> 마우스로 높이 (객체스냅이 잡히면 그 점의 높이,
//         숫자를 치면 그 값, 음수는 반대쪽) -> 클릭 / Enter. 스케치는 남고 솔리드와 피처로 묶인다.
//   보스 / 포켓: 돌출 솔리드 하나 + 닫힌 스케치를 고르고 시작 -> 같은 방법으로 높이 / 깊이.
// 한 번에 한 번의 실행 취소. Esc 로 취소.
class ExtrudeTool {
public:
    enum class Mode { Extrude, Boss, Pocket };
    enum class State { Idle, PickSketch, Height };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        const lot_osnap::Snap& snap;
        float width;
        float height;
    };

    // 선택에서 시작한다. 시작했으면 true (아니면 이유를 로그로).
    bool start(Mode mode, const std::set<LotGameObject::id_t>& selection, const LotGameObject::Map& objects);
    void cancel();
    void update(const Context& ctx, EditHistory& history, lot_web_device& device);
    // Enter: 숫자를 쳤으면 그 값, 아니면 지금 마우스 높이로 확정
    void finish(const Context& ctx, EditHistory& history, lot_web_device& device);
    void setNumberBuffer(const std::string& s) { number_ = s; }

    bool isActive() const { return state_ != State::Idle; }
    bool wantsNumber() const { return state_ == State::Height; }
    Mode mode() const { return mode_; }
    std::string hint() const;
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    // 방금 만든 / 고친 솔리드 (선택 갱신용). 없으면 빈 집합.
    std::set<LotGameObject::id_t> consumeResult() { auto r = std::move(result_); result_.clear(); return r; }

private:
    struct Source {
        LotGameObject::id_t id;
        std::vector<vec3> pts;   // 월드
        vec3 normal;
        vec3 center;
    };
    float mouseHeight(const Context& ctx) const;
    void commit(const Context& ctx, float h, EditHistory& history, lot_web_device& device);

    Mode mode_ = Mode::Extrude;
    State state_ = State::Idle;
    std::vector<Source> sources_;
    LotGameObject::id_t solid_ = LotGameObject::kInvalidId;   // 보스 / 포켓 대상
    vec3 axis_{0.0f, 0.0f, 1.0f};    // 높이를 재는 방향 (월드)
    vec3 center_{0.0f, 0.0f, 0.0f};
    float height_ = 1.0f;
    float startMouseY_ = 0.0f;
    bool haveStartY_ = false;
    std::string number_;
    std::set<LotGameObject::id_t> result_;
};
