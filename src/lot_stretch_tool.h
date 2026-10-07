#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"
#include "lot_osnap.h"
#include "lot_sketch_tool.h"   // SketchPlane

#include <string>
#include <vector>

class LineRenderSystem;
class MouseInput;

// 늘이기 (AutoCAD STRETCH, 네이티브 first_app/stretch.cpp 와 같은 흐름).
//   걸침 창 두 모서리 -> 기준점 -> 둘째 점 (클릭 · '@dx,dy' · 커서 방향 거리)
//   창 안의 꼭짓점만 옮긴다. 통째로 들어간 객체는 그대로 이동. 호는 끝점/중간점을 옮긴 뒤 세 점을
//   지나게 다시 맞추고, 원 · 문자 · 광원 · 메시는 기준점(중심)이 창 안이면 이동, 치수는 측정점마다.
//   한 번의 실행 취소. Esc 로 취소.
class StretchTool {
public:
    enum class State { Idle, Corner1, Corner2, Base, Second };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        const lot_osnap::Snap& snap;
        float width;
        float height;
    };

    void start(const LotCamera& camera);
    void cancel();
    void update(const Context& ctx, EditHistory& history);
    // 명령행: '@dx,dy' (작업평면 축) 또는 거리 (커서 방향). 받았으면 true.
    bool typed(const std::string& text, const Context& ctx, EditHistory& history);
    // 키보드로 모은 숫자 + Enter (둘째 점 단계의 거리)
    void setNumberBuffer(const std::string& s) { number_ = s; }
    bool finish(const Context& ctx, EditHistory& history);   // Enter. 끝냈으면 true

    bool isActive() const { return state_ != State::Idle; }
    bool wantsNumber() const { return state_ == State::Second; }
    bool takesClicks() const { return isActive(); }
    std::string hint() const;
    // 기준점 (둘째 점 단계) - 직교/극좌표/수직 스냅 기준
    const vec3* referencePoint() const { return state_ == State::Second ? &base_ : nullptr; }
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

private:
    struct Grip {               // 옮길 것 하나
        LotGameObject::id_t id;
        enum class Kind { Whole, Vertex, ArcStart, ArcMid, ArcEnd, DimP1, DimP2, DimLine } kind;
        size_t index = 0;       // Vertex 의 점 번호
    };
    bool cursorPoint(const Context& ctx, vec3& out, bool corrected) const;
    void collect(const Context& ctx);
    // 미리보기와 적용이 같은 계산: 옮긴 뒤의 월드 점 / 곡선 (적용하면 objects 를 고친다)
    void applyTo(LotGameObject::Map& objects, const vec3& d, bool commit, EditHistory* history,
                 std::vector<std::pair<vec3, vec3>>* preview) const;
    bool commit(const Context& ctx, const vec3& d, EditHistory& history);

    State state_ = State::Idle;
    SketchPlane plane_;
    float c1x_ = 0.0f, c1y_ = 0.0f, c2x_ = 0.0f, c2y_ = 0.0f;   // 창 (화면 픽셀)
    vec3 base_{};
    vec3 cursor_{};
    std::string number_;
    std::vector<Grip> grips_;
};
