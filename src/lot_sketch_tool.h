#pragma once

#include "lot_camera.h"
#include "lot_game_object.h"
#include "lot_math.h"
#include "lot_osnap.h"
#include "lot_picking.h"

#include <memory>
#include <string>
#include <vector>

class LineRenderSystem;
class MouseInput;
class PolylineRenderSystem;

// 스케치 도구 - 클릭으로 선/사각형/폴리라인을 그린다.
//
// Vulkan 쪽은 LineManager / RectangleManager / PolylineManager 가 각자 상태
// 머신과 입력 처리를 따로 들고 있어서, 도구를 하나 추가할 때마다 "어느 도구든
// 활성인가" 를 묻는 OR 게이트 N 곳을 전부 고쳐야 했다 (CLAUDE.md 의 사고 기록).
// 여기서는 처음부터 공통 부모(SketchTool)와 컨트롤러(SketchController)로 묶어
// 게이트가 anyActive() 한 곳이 되게 한다. 도구는 점을 받았을 때 무엇을 만들지만
// 정의하고, 평면 교차·스냅·클릭 소비·프리뷰 그리기는 컨트롤러가 한다.

// 스케치 평면. 클릭한 화면 점을 이 평면에 내려 월드 점으로 만든다.
//
// Z-up 좌표계(+X 오른쪽, +Y 앞, +Z 위, 바닥 = XY) 기준 - Vulkan 쪽 getPlaneVectors 와 같다:
//   Top 뷰 -> XY (바닥, 법선 Z), Front -> XZ (법선 Y), Right -> YZ (법선 X).
// 뷰 프리셋 이름 대신 카메라가 보는 방향의 지배 축으로 고르므로 궤도를 돌린
// 뒤에도 '가장 정면으로 보이는' 평면이 잡힌다. 원점은 항상 월드 원점을 지난다.
struct SketchPlane {
    vec3 origin{0.0f, 0.0f, 0.0f};
    vec3 right{1.0f, 0.0f, 0.0f};
    vec3 up{0.0f, 0.0f, 1.0f};
    vec3 normal{0.0f, 1.0f, 0.0f};
    const char* name = "XZ (front)";

    static SketchPlane fromCamera(const LotCamera& camera);

    // 레이와 평면의 교점. 평행이면 false.
    bool intersect(const lot_pick::Ray& ray, vec3& out) const;
};

// 원/호를 점으로 쪼갠다. 점 = center + r (cos t right + sin t up), t 는 start 에서 end 까지
// (end < start 면 시계 방향). 원은 start 0, end 2π 를 주고 closed 로 그린다.
// 세그먼트 수는 각도에 비례하되 최소 8 - 작은 호가 각지지 않게.
std::vector<vec3> tessellateArc(const vec3& center, float radius, const vec3& right,
                                const vec3& up, float start, float end, bool includeEnd = true);

// 도구 하나. 활성 동안 점을 받아 모으고, 조건이 차면 스케치 오브젝트를 만든다.
class SketchTool {
public:
    virtual ~SketchTool() = default;

    virtual const char* name() const = 0;

    // 클릭(또는 스냅) 점 하나. 도구가 알아서 확정하거나 계속 모은다.
    virtual void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) = 0;

    // Enter - 모은 것으로 끝낼 수 있으면 확정. 도구가 계속 열려 있어야 하면 false.
    virtual bool onFinish(LotGameObject::Map& objects) = 0;

    // 하나 확정하면 도구가 닫히나. 선은 AutoCAD LINE 처럼 이어 그리므로 false,
    // 사각형/폴리라인은 RECTANG/PLINE 처럼 하나로 끝난다.
    virtual bool endsAfterCommit() const { return true; }

    // 프리뷰: 모은 점 + 커서. 확정 전 모양을 그대로 보여준다.
    virtual void preview(const vec3& cursor, const SketchPlane& plane,
                         std::vector<vec3>& outPoints, bool& outClosed) const = 0;

    void begin() { points_.clear(); }
    void cancel() { points_.clear(); }
    bool hasPoints() const { return !points_.empty(); }
    const std::vector<vec3>& points() const { return points_; }

    // 이번 프레임 확정된 오브젝트 id. 없으면 kInvalidId. 읽으면 비워진다.
    LotGameObject::id_t consumeCommittedId() {
        const auto id = committedId_;
        committedId_ = LotGameObject::kInvalidId;
        return id;
    }

protected:
    // 점 목록을 스케치 오브젝트로. 무게중심을 translation 으로, 점은 상대 좌표로.
    // curve 를 주면 (원/호) 그 정의도 같이 - 기준점은 무게중심 대신 curve.center 로 잡고
    // center 는 로컬 원점이 된다.
    LotGameObject::id_t commit(const std::vector<vec3>& worldPoints, bool closed,
                               const vec3& color, LotGameObject::Map& objects,
                               const LotGameObject::Curve* curve = nullptr);

    std::vector<vec3> points_;
    LotGameObject::id_t committedId_ = LotGameObject::kInvalidId;
};

// 선 (L). 두 점마다 선분 하나. AutoCAD LINE 처럼 끝점에서 다음 선분이 이어진다
// - Enter/Esc 로 끝낸다. 선분마다 별개 오브젝트다.
class LineTool : public SketchTool {
public:
    const char* name() const override { return "line"; }
    bool endsAfterCommit() const override { return false; }
    void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) override;
    bool onFinish(LotGameObject::Map& objects) override;
    void preview(const vec3& cursor, const SketchPlane& plane,
                 std::vector<vec3>& outPoints, bool& outClosed) const override;
};

// 사각형 (B). 마주보는 두 꼭짓점. 변은 평면의 right/up 축에 나란하다.
class RectangleTool : public SketchTool {
public:
    const char* name() const override { return "rectangle"; }
    void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) override;
    bool onFinish(LotGameObject::Map& objects) override;
    void preview(const vec3& cursor, const SketchPlane& plane,
                 std::vector<vec3>& outPoints, bool& outClosed) const override;

    // 두 꼭짓점에서 네 꼭짓점 (p0 -> right 방향 -> 대각 -> up 방향 순서)
    static std::vector<vec3> corners(const vec3& p0, const vec3& p1, const SketchPlane& plane);
};

// 폴리라인 (N). 점을 계속 찍고 Enter 로 끝낸다 (열림). 첫 점을 다시 찍으면
// 닫힌 폴리라인으로 확정한다 (점 3개 이상일 때).
class PolylineTool : public SketchTool {
public:
    const char* name() const override { return "polyline"; }
    void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) override;
    bool onFinish(LotGameObject::Map& objects) override;
    void preview(const vec3& cursor, const SketchPlane& plane,
                 std::vector<vec3>& outPoints, bool& outClosed) const override;

    // 커서가 첫 점에 이 거리(월드) 안이면 '닫기'로 본다. 컨트롤러가 픽셀에서 환산해 넣는다.
    float closeRadius = 0.0f;
};

// 원 (C). 중심, 반지름 점. 하나로 끝.
class CircleTool : public SketchTool {
public:
    const char* name() const override { return "circle"; }
    void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) override;
    bool onFinish(LotGameObject::Map& objects) override;
    void preview(const vec3& cursor, const SketchPlane& plane,
                 std::vector<vec3>& outPoints, bool& outClosed) const override;
};

// 호 (A). 세 점 - 시작, 호 위의 한 점, 끝 (AutoCAD 의 3P). 하나로 끝.
class ArcTool : public SketchTool {
public:
    const char* name() const override { return "arc"; }
    void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) override;
    bool onFinish(LotGameObject::Map& objects) override;
    void preview(const vec3& cursor, const SketchPlane& plane,
                 std::vector<vec3>& outPoints, bool& outClosed) const override;

    // 세 점을 지나는 호의 정의. 세 점이 한 직선이면 false.
    static bool solve(const vec3& a, const vec3& b, const vec3& c, const SketchPlane& plane,
                      LotGameObject::Curve& out);
};

// 정다각형 (G). 중심, 꼭짓점 하나 (내접 - 꼭짓점이 원 위). [ / ] 로 변 수. 하나로 끝.
class PolygonTool : public SketchTool {
public:
    const char* name() const override { return "polygon"; }
    void onPoint(const vec3& p, const SketchPlane& plane, LotGameObject::Map& objects) override;
    bool onFinish(LotGameObject::Map& objects) override;
    void preview(const vec3& cursor, const SketchPlane& plane,
                 std::vector<vec3>& outPoints, bool& outClosed) const override;

    static std::vector<vec3> vertices(const vec3& center, const vec3& vertex,
                                      const SketchPlane& plane, int sides);
    int sides = 6;
};

// 도구 레지스트리 + 입력. 렌더 루프가 프레임마다 update 를 부른다.
class SketchController {
public:
    enum class Kind { Line, Rectangle, Polyline, Circle, Arc, Polygon };

    struct Context {
        const LotCamera& camera;
        MouseInput& mouse;
        LotGameObject::Map& objects;
        const lot_osnap::Snap& snap;  // 이번 프레임 커서 아래 스냅 (EditController 가 찾은 것)
        float width;
        float height;
    };

    SketchController();

    // 도구 시작. 다른 도구가 열려 있으면 취소한다 - 둘이 동시에 활성인 일은 없다.
    // 평면은 시작 순간의 카메라로 정하고 도구가 끝날 때까지 고정한다.
    void start(Kind kind, const LotCamera& camera);
    void cancel();

    // 활성 도구가 있나. 게이트는 이것 하나다 (클릭, C/Del, 스냅 제외 등).
    bool anyActive() const { return active_ != nullptr; }
    const char* activeName() const { return active_ ? active_->name() : ""; }
    // 열린 도구의 Kind 번호, 없으면 -1 (툴바 표시용)
    int activeKind() const;
    const SketchPlane& plane() const { return plane_; }

    // 사용자에게 보일 다음 할 일. 도구가 없으면 빈 문자열.
    std::string hint() const;

    // 프레임당 한 번. 활성이면 왼쪽 클릭을 소비한다 (EditController 에 가지 않는다).
    void update(const Context& ctx);

    // Enter / Esc. 키 컨트롤러가 '누른 순간'을 넘겨준다.
    void finish(LotGameObject::Map& objects);

    // 프리뷰 + 커서 표시. 활성일 때만 무언가 그린다.
    void drawPreview(PolylineRenderSystem& polylines, LineRenderSystem& lines,
                     const Context& ctx) const;

    // 마지막으로 확정된 오브젝트 id (프레임당 한 번 소비). 실행 취소 등록용.
    LotGameObject::id_t consumeCommittedId();

    // 픽셀 -> 월드 환산에 쓰는 커서 반경 (폴리라인 닫기 판정)
    float closeRadiusPx = 10.0f;

    // 다각형 변 수 (3 ~ 32). 도구가 열려 있든 아니든 바꿀 수 있다.
    void changePolygonSides(int delta);
    int polygonSides() const;

    // 열린 도구의 직전 점 (수직 스냅 기준, 직교 트랙킹 기준). 없으면 nullptr.
    const vec3* referencePoint() const;

    // 직교 트랙킹 (F8): 직전 점에서 작업평면의 한 축으로만 나가게 커서를 묶는다.
    // 스냅이 잡혔으면 스냅이 이긴다 (AutoCAD 와 같다).
    bool orthoTracking = false;

private:
    // 커서의 월드 점: 스냅이 있으면 스냅 점, 없으면 평면 교점. 평행이면 false.
    bool cursorPoint(const Context& ctx, vec3& out) const;

    std::unique_ptr<LineTool> line_;
    std::unique_ptr<RectangleTool> rectangle_;
    std::unique_ptr<PolylineTool> polyline_;
    std::unique_ptr<CircleTool> circle_;
    std::unique_ptr<ArcTool> arc_;
    std::unique_ptr<PolygonTool> polygon_;
    SketchTool* active_ = nullptr;
    SketchPlane plane_;
    LotGameObject::id_t lastCommitted_ = LotGameObject::kInvalidId;
};
