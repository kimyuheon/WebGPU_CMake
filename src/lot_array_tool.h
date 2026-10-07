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

// 배열 (네이티브 modify/array_manager + lot_array_dialog 와 같은 동작).
//
// 직사각형(열 x 행, 간격) 또는 원형(개수, 채울 각, 항목 회전, 중심). 떠 있는 대화상자에서 값을
// 바꾸면 미리보기가 바로 바뀌고 [생성] 으로 확정한다. 결과는 독립된 복사본 - 한 번에 실행 취소.
//   - 평면은 시작 때의 카메라 작업평면: 열은 오른쪽, 행은 위쪽, 원형은 법선 둘레.
//   - 원형 360 도는 마지막이 첫 것과 겹치지 않게 360/개수, 그 밖은 각/(개수-1) (AutoCAD 와 같다).
//   - 원형 중심 기본값은 선택의 가운데. [중심 클릭 지정] 이면 다음 클릭(스냅 가능)이 중심.
//   - 복사본은 2000 개까지.
class ArrayTool {
public:
    enum class State { Idle, WaitSelect, Dialog, PickCenter };

    struct Params {
        bool polar = false;
        int cols = 3, rows = 2;
        float dx = 2.0f, dy = 2.0f;
        int count = 6;
        float angle = 360.0f;     // 도
        bool rotateItems = true;
        bool centerAuto = true;   // 선택 가운데를 따라간다
        vec3 center{};
    };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        const lot_osnap::Snap& snap;
        float width;
        float height;
    };

    // 선택이 있으면 대화상자를 열고, 없으면 고르고 Enter 를 기다린다.
    void start(const std::set<LotGameObject::id_t>& selection, const LotCamera& camera,
               const LotGameObject::Map& objects);
    // Enter: 선택을 기다리던 중이면 대화상자를 연다. 대화상자 중이면 생성.
    void enter(const std::set<LotGameObject::id_t>& selection, LotGameObject::Map& objects,
               EditHistory& history);
    void cancel();   // Esc: 중심 고르기면 대화상자로, 아니면 닫기
    void close();    // 다른 도구를 시작할 때 - 무조건 닫는다

    void setParams(const Params& p);
    const Params& params() const { return params_; }
    void beginPickCenter();
    // 만들고 대화상자를 닫는다. 만든 id 들 (선택 갱신용).
    std::set<LotGameObject::id_t> create(LotGameObject::Map& objects, EditHistory& history);

    void update(const Context& ctx);   // 중심 고르기 클릭
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    State state() const { return state_; }
    bool isActive() const { return state_ != State::Idle; }
    bool takesClicks() const { return state_ == State::PickCenter; }
    std::string hint() const;

    // 대화상자에 보낼 것 (열림 · 값 · 중심). 바뀌었으면 JSON, 아니면 빈 문자열.
    std::string dialogJson();
    void invalidateDialog() { lastJson_.clear(); }

private:
    void openDialog(const std::set<LotGameObject::id_t>& selection, const LotGameObject::Map& objects);
    // 복사본 i 의 월드 변환을 원본 점 p 에 적용 (미리보기와 생성이 같은 규칙을 쓴다)
    int instanceCount() const;
    void placement(int i, const vec3& visualCenter, quat& rot, vec3& pivot, vec3& offset) const;

    State state_ = State::Idle;
    Params params_;
    vec3 right_{1.0f, 0.0f, 0.0f}, up_{0.0f, 1.0f, 0.0f}, normal_{0.0f, 0.0f, 1.0f};
    std::vector<LotGameObject::id_t> sources_;
    vec3 selectionCenter_{};
    std::string lastJson_;
};
