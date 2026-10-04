#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"

#include <string>
#include <vector>

class LineRenderSystem;
class MouseInput;

// 간격띄우기 (AutoCAD OFFSET). 흐름:
//   도구 시작 -> 거리 입력 + Enter (Enter 만 치면 지난 거리)
//   -> 객체 클릭 -> 커서 쪽으로 미리보기 -> 클릭하면 그쪽에 평행 사본
//   -> 다음 객체 (반복) ... Esc / Enter 로 끝
//
// 선 · 폴리선은 변마다 평행이동한 뒤 이웃 변끼리 연장해 꼭짓점을 맞붙인다 (너무 뾰족하면
// 두 점으로 깎는다). 원 · 호는 중심과 각을 두고 반지름만 ± 거리. 계산은 월드 좌표로,
// 객체가 놓인 평면(곡선 축 / 점들의 법선, 곧은 선이면 시작 때의 작업평면) 위에서 한다.
class OffsetTool {
public:
    enum class State { Idle, WaitingDistance, PickObject, PickSide };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        float width;
        float height;
    };

    // defaultDistance: 아직 한 번도 안 썼을 때의 기본 거리 (도면 크기에 맞춘 값)
    void start(const LotCamera& camera, float defaultDistance);
    void cancel();

    // 프레임당 한 번. 활성이면 왼쪽 클릭을 가져간다.
    void update(const Context& ctx, EditHistory& history);
    // Enter: 거리 단계면 입력값(없으면 기본값)으로 넘어가고, 그 뒤면 도구를 닫는다.
    void finish();

    void setNumberBuffer(const std::string& s) { number_ = s; }
    bool isActive() const { return state_ != State::Idle; }
    bool wantsNumber() const { return state_ == State::WaitingDistance; }
    float distance() const { return distance_; }
    std::string hint() const;

    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    // 방금 만든 사본 (선택 갱신용). 없으면 kInvalidId.
    LotGameObject::id_t consumeCreated() {
        const auto id = created_;
        created_ = LotGameObject::kInvalidId;
        return id;
    }

    // 간격띄운 모양 (월드 점). 원/호면 curveOut 도 채운다 (center/right/up 월드, kind None 이면 곡선 아님).
    // sidePoint 쪽으로 distance 만큼. 만들 수 없으면 false (반지름이 0 이하 등).
    static bool offsetShape(const LotGameObject& src, float distance, const vec3& sidePoint,
                            const vec3& fallbackNormal, std::vector<vec3>& pointsOut, bool& closedOut,
                            LotGameObject::Curve& curveOut);

private:
    bool sidePoint(const Context& ctx, const LotGameObject& obj, vec3& out) const;

    State state_ = State::Idle;
    float distance_ = 1.0f;
    bool distanceSet_ = false;   // 한 번이라도 정했으면 다음 시작 때 그 값을 기본으로
    std::string number_;
    vec3 workNormal_{0.0f, 0.0f, 1.0f};
    LotGameObject::id_t source_ = LotGameObject::kInvalidId;
    LotGameObject::id_t created_ = LotGameObject::kInvalidId;
};
