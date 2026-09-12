#pragma once

#include "lot_camera.h"
#include "lot_frame_info.h"
#include "lot_game_object.h"
#include "lot_math.h"
#include "lot_osnap.h"
#include "lot_picking.h"

#include <set>
#include <utility>
#include <vector>

class GizmoRenderSystem;
class LineRenderSystem;
class MouseInput;

// 선택과 편집 - 클릭/드래그 선택, 기즈모 드래그, 스냅.
//
// main 의 렌더 루프에서 뽑아냈다. 복사/축척/회전이 전부 '선택 집합에 대한
// 조작'이라 한 곳에 모아야 서로 꼬이지 않는다.
//
// 선택은 집합이다. 클릭은 교체, Shift+클릭은 토글, 빈 곳에서 끌면 박스 선택
// (왼->오른쪽은 완전히 들어온 것만 = window, 오른->왼쪽은 걸치기만 해도 = crossing,
// CAD 관례). 기즈모는 선택 집합의 중심에 붙고 드래그하면 전부 같이 움직인다.
class EditController {
public:
    using id_t = LotGameObject::id_t;

    // 프레임마다 넘기는 것들
    struct Context {
        LotCamera& camera;
        MouseInput& mouse;
        GizmoRenderSystem& gizmo;
        LotGameObject::Map& objects;
        float width;
        float height;
    };

    // 입력 처리. 프레임당 한 번, 카메라가 갱신되기 전에 부른다
    // (레이는 이전 프레임 카메라로 - 한 프레임 차이는 눈에 띄지 않는다).
    void update(const Context& ctx);

    // 오버레이(선택 상자, 박스 선택 사각형, 스냅 마커)를 선 시스템에 넣는다.
    void drawOverlay(LineRenderSystem& lines, const Context& ctx) const;

    // 기즈모. 선택이 있을 때만 그린다.
    void drawGizmo(FrameInfo& frame, GizmoRenderSystem& gizmo) const;

    const std::set<id_t>& selection() const { return selection_; }
    bool hasSelection() const { return !selection_.empty(); }
    bool isSelected(id_t id) const { return selection_.count(id) != 0; }

    // 선택 집합의 중심 (translation 의 평균). 기즈모 위치 = 편집 기준점.
    vec3 pivot(const LotGameObject::Map& objects) const;

    // 선택을 바깥에서 바꿀 때 (복사 뒤 사본을 선택하는 등)
    void setSelection(std::set<id_t> ids) { selection_ = std::move(ids); }
    void clearSelection() { selection_.clear(); }

    const lot_osnap::Snap& snap() const { return snap_; }

    float snapRadiusPx = 14.0f;
    float snapMarkerPx = 7.0f;

private:
    // 기즈모 드래그 상태.
    //
    // 누른 순간의 기준점(선택 중심)과 각 오브젝트의 변환을 들고 있는다.
    // 매 프레임 '시작값 + 현재 오프셋'으로 계산해야 오차가 쌓이지 않는다.
    struct Drag {
        bool active = false;
        int handle = -1;              // 0~2 축, 3~5 평면
        vec3 pivotStart{};            // 누른 순간 선택 중심
        float startS = 0.0f;          // (축) 축 위 파라미터
        vec3 startHit{};              // (평면) 평면 위 교점
        std::vector<std::pair<id_t, TransformComponent>> startTransforms;
    };

    // 박스 선택 상태
    struct Marquee {
        bool active = false;
        float x0 = 0.0f;
        float y0 = 0.0f;
    };

    void beginGizmoDrag(const Context& ctx, const lot_pick::Ray& ray, int handle);
    void updateGizmoDrag(const Context& ctx, const lot_pick::Ray& ray);
    void endGizmoDrag(const Context& ctx);
    void applyTranslation(const Context& ctx, const vec3& delta);

    void finishMarquee(const Context& ctx, float x1, float y1, bool additive);

    std::set<id_t> selection_;
    Drag drag_;
    Marquee marquee_;
    lot_osnap::Snap snap_;
};
