#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_history.h"
#include "lot_math.h"
#include "lot_osnap.h"
#include "lot_sketch_tool.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

class LineRenderSystem;
class MouseInput;

// 기준점 방식 변환 (AutoCAD 의 MOVE / COPY / ROTATE / SCALE / MIRROR).
//
// 기즈모는 '잡고 끌기'라 정밀한 자리를 맞추기 어렵다. CAD 는 기준점을 찍고
// (스냅으로 정확히), 목적점을 찍거나 (역시 스냅) 숫자를 친다. 흐름:
//   도구 시작 -> 기준점 클릭 -> 커서를 따라 미리보기 -> 클릭 확정 / 숫자+Enter / Esc 복원
// 이동은 기준점->커서 변위, 회전은 기준점 둘레 각(작업평면 법선 축), 축척은 기준점을
// 앵커로 한 배율. 복사는 이동과 같되 확정 때 원본을 두고 사본을 놓으며 도구가 계속
// 열려 있어 여러 개를 찍을 수 있다.
// 대칭은 기준점이 대칭축의 첫 점이고, 둘째 점을 찍으면 대칭 사본을 만든다 (Shift+클릭이면
// 원본을 지운다 - AutoCAD 의 '원본 지우기? 예'). 미리보기는 객체를 건드리지 않고 선으로 그린다.
//
// 작업평면은 시작 순간의 카메라로 정한다 (SketchPlane::fromCamera). 커서 점은
// 스냅이 있으면 스냅 점, 없으면 평면 교점 - 스케치 도구와 같은 규칙이다.
// Vulkan 쪽 transform_tool 과 같은 자리다 (ROTATE_AXIS / 직교 트랙킹은 아직 없다).
class TransformTool {
public:
    enum class Mode { None, Move, Copy, Rotate, Scale, Mirror };
    enum class State { Idle, WaitingBase, Previewing };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        const lot_osnap::Snap& snap;
        float width;
        float height;
    };

    // 선택이 비어 있으면 시작하지 않는다 (false).
    bool start(Mode mode, const std::set<LotGameObject::id_t>& selection, const LotCamera& camera,
               const LotGameObject::Map& objects);

    // Esc: 미리보기를 원상복구하고 닫는다.
    void cancel(LotGameObject::Map& objects);

    // 프레임당 한 번. 활성이면 왼쪽 클릭을 소비한다.
    void update(const Context& ctx, EditHistory& history);

    // Enter: 숫자가 있으면 그 값으로, 없으면 지금 미리보기로 확정.
    void finish(const Context& ctx, EditHistory& history);

    // 숫자 입력 (키 컨트롤러가 미리보기 중 모아준 문자열). 비어 있으면 없음.
    void setNumberBuffer(const std::string& s) { number_ = s; }

    bool isActive() const { return state_ != State::Idle; }
    bool isPreviewing() const { return state_ == State::Previewing; }
    Mode mode() const { return mode_; }
    int modeIndex() const { return isActive() ? static_cast<int>(mode_) : -1; }
    const char* modeName() const;

    // 툴바/안내문
    std::string hint() const;

    // 기준점 (미리보기 중) - 수직 스냅 / 직교 트랙킹 기준. 없으면 nullptr.
    const vec3* referencePoint() const { return isPreviewing() ? &base_ : nullptr; }


    // 오버레이: 기준점 마커 + 고무줄 (기준점 -> 커서).
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    // 복사가 만든 사본들 (확정 직후 프레임에 한 번 비워진다) - 선택 갱신용.
    std::set<LotGameObject::id_t> consumeCreated() {
        std::set<LotGameObject::id_t> out = std::move(created_);
        created_.clear();
        return out;
    }

private:
    bool cursorPoint(const Context& ctx, vec3& out) const;

    // 미리보기: 시작 변환에서 지금 커서로. 확정 전까지 매 프레임 다시 계산한다.
    void applyPreview(const vec3& cursor, LotGameObject::Map& objects);
    void applyMove(const vec3& delta, LotGameObject::Map& objects);
    void applyRotate(float angle, LotGameObject::Map& objects);
    void applyScale(float factor, LotGameObject::Map& objects);

    // 숫자로 확정 (거리 / 도 / 배율). 이동 거리의 방향은 지금 커서 방향.
    bool applyNumber(float value, const vec3& cursor, LotGameObject::Map& objects);

    void restore(LotGameObject::Map& objects);

    // 대칭: 축(base_ -> p) 이 정하는 평면에 대한 반사. 축이 너무 짧으면 false.
    bool mirrorAxis(const vec3& p, vec3& normalOut) const;
    // obj 를 그 평면에 대해 반사한 사본 (원본은 그대로). 문자는 읽히게 둔다 (MIRRTEXT 0).
    static LotGameObject mirrored(const LotGameObject& obj, const vec3& origin, const vec3& n);
    void confirmMirror(const Context& ctx, EditHistory& history, bool eraseSource);
    vec3 mirrorEnd_{};      // 대칭축 둘째 점 (미리보기)
    bool mirrorValid_ = false;
    void confirm(const Context& ctx, EditHistory& history);

    Mode mode_ = Mode::None;
    State state_ = State::Idle;
    SketchPlane plane_;
    vec3 base_{};
    vec3 pivot_{};          // 선택 중심 (축척 기준 거리, 회전 감도)
    vec3 trackEnd_{};       // 고무줄 끝 (미리보기가 갱신)
    float baseDist_ = 0.0f; // 축척: 배율 1 인 기준 거리 (pivot <-> base, 너무 짧으면 첫 커서 거리)
    float minRef_ = 0.0f;   // 축척: 이보다 짧은 기준 거리는 쓰지 않는다 (화면 12px 어치)
    float refAngle_ = 0.0f; // 회전: 기준점 직후 첫 커서 각 (0 기준)
    bool refValid_ = false;
    float lastValue_ = 0.0f;  // 안내문용 (거리 / 도 / 배율)
    std::string number_;
    std::vector<std::pair<LotGameObject::id_t, TransformComponent>> saved_;
    std::set<LotGameObject::id_t> created_;
};
