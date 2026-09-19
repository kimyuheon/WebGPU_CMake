#pragma once

#include "lot_math.h"

#include <cmath>

// 카메라 - 투영 행렬과 뷰 행렬, 그리고 CAD 궤도 상태를 들고 있는 순수 계산 클래스.
// GPU 리소스를 잡지 않으므로 헤더 하나로 끝난다.
//
// 두 가지 뷰 모드가 있다 (Vulkan 쪽 ViewMode 와 같다):
//   Fps - 카메라를 게임 오브젝트처럼 위치 + 회전으로 들고 다닌다 (WASD).
//   Cad - 타깃을 중심으로 궤도를 돈다 (우클릭 궤도, 중클릭 팬, 휠 줌). CAD 기본.
class LotCamera {
public:
    enum class ViewMode { Fps, Cad };

    // 표준 CAD 뷰. 이 엔진의 좌표계(+X 오른쪽, +Y 아래, +Z 앞, 바닥 = XZ 평면)에서
    // Front 는 -Z 에서 +Z 를 본다, Top 은 위(-Y)에서 내려다본다.
    enum class CadViewType { Front, Back, Top, Bottom, Right, Left, Isometric };

    void setViewMode(ViewMode m) { viewMode_ = m; }
    ViewMode getViewMode() const { return viewMode_; }
    bool isCadMode() const { return viewMode_ == ViewMode::Cad; }

    // ---- CAD 궤도 ----
    //
    // orbitRotation_ 규약: forward = q * (0, 0, 1), up = q * (0, -1, 0), right = q * (1, 0, 0).
    // 카메라 위치는 target - forward * orbitDistance. 뷰 행렬은 updateCadView 가 만든다.

    // 턴테이블 궤도: yaw 는 월드 위 축(-Y) 둘레, pitch 는 카메라 오른쪽 축 둘레 (라디안).
    // 자유 트랙볼과 달리 롤이 쌓이지 않아 수평선이 항상 수평이다 (AutoCAD 3DORBIT 과 같다).
    // 극점을 넘어가면 카메라가 뒤집히므로 그 직전에서 pitch 를 막는다.
    void orbitAroundTarget(float yaw, float pitch) {
        const vec3 worldUp{0.0f, -1.0f, 0.0f};
        const quat qYaw = quat::angleAxis(yaw, worldUp);
        const vec3 right = rotate(orbitRotation_, vec3{1.0f, 0.0f, 0.0f});
        const quat qPitch = quat::angleAxis(pitch, right);

        quat next = normalize(qYaw * qPitch * orbitRotation_);
        const vec3 nextUp = rotate(next, vec3{0.0f, -1.0f, 0.0f});
        if (dot(nextUp, worldUp) < kMinUpDot) {
            next = normalize(qYaw * orbitRotation_);  // pitch 는 버리고 yaw 만
        }
        orbitRotation_ = next;
        presetView_ = false;  // 돌리는 순간 더 이상 표준 뷰가 아니다
        updateCadView();
    }

    // 휠 한 노치 = 약 8% (AutoCAD 감각). notches > 0 이 줌 인.
    // 원근에서는 거리, 직교에서는 호출자가 halfHeight 를 같은 비율로 줄인다 (zoomFactor).
    static float zoomFactor(float notches) {
        if (notches > kMaxNotchesPerFrame) notches = kMaxNotchesPerFrame;
        if (notches < -kMaxNotchesPerFrame) notches = -kMaxNotchesPerFrame;
        return std::pow(kZoomBase, notches);
    }
    void zoomToTarget(float notches) {
        orbitDistance_ *= zoomFactor(notches);
        if (orbitDistance_ < kMinOrbitDistance) orbitDistance_ = kMinOrbitDistance;
        if (orbitDistance_ > maxOrbitDistance_) orbitDistance_ = maxOrbitDistance_;
        updateCadView();
    }

    // 구(중심, 반지름)가 화면에 꽉 차게 타깃과 거리를 잡는다 (Zoom Extents).
    // 거리 = r / sin(fov/2) 에 여유 15%. 줌 아웃 한계도 씬 크기에 맞춰 늘린다 -
    // mm 단위 도면처럼 수천 단위 씬은 고정 한계(60)로는 못 담는다.
    void frame(const vec3& center, float radius, float fovY) {
        if (radius < 1e-4f) radius = 1e-4f;
        target_ = center;
        const float dist = radius / std::sin(fovY * 0.5f) * 1.15f;
        maxOrbitDistance_ = std::fmax(kMaxOrbitDistance, dist * 4.0f);
        orbitDistance_ = std::fmax(kMinOrbitDistance, dist);
        updateCadView();
    }
    float getMaxOrbitDistance() const { return maxOrbitDistance_; }

    // 픽셀 단위 팬. 타깃 깊이에서 화면 1 픽셀이 월드 몇 단위인지로 환산하므로
    // 드래그한 만큼 정확히 장면이 따라온다 (원근/직교 모두).
    void panTarget(float dxPx, float dyPx, float viewportHeight) {
        const float wpp = worldPerPixel(target_, viewportHeight);
        target_ = target_ - (getRight() * dxPx + getDown() * dyPx) * wpp;
        updateCadView();
    }

    // 표준 뷰로 리셋. 타깃은 원점, 거리는 기본값.
    void resetCadView(CadViewType type) {
        target_ = vec3{0.0f, 0.0f, 0.0f};
        orbitDistance_ = kDefaultOrbitDistance;
        const float kHalfPi = 1.57079632679f;
        const vec3 X{1.0f, 0.0f, 0.0f}, Y{0.0f, 1.0f, 0.0f};
        switch (type) {
        case CadViewType::Front:  orbitRotation_ = quat::identity(); break;
        case CadViewType::Back:   orbitRotation_ = quat::angleAxis(2.0f * kHalfPi, Y); break;
        case CadViewType::Top:    orbitRotation_ = quat::angleAxis(-kHalfPi, X); break;
        case CadViewType::Bottom: orbitRotation_ = quat::angleAxis(kHalfPi, X); break;
        case CadViewType::Right:  orbitRotation_ = quat::angleAxis(-kHalfPi, Y); break;
        case CadViewType::Left:   orbitRotation_ = quat::angleAxis(kHalfPi, Y); break;
        case CadViewType::Isometric:
            // 앞-왼쪽-위에서 내려다본다. 세 축이 같은 각으로 보이는 등각.
            setViewFromDirection(normalize(vec3{-1.0f, -1.0f, -1.0f}));
            currentViewType_ = type;
            presetView_ = true;
            return;
        }
        currentViewType_ = type;
        presetView_ = true;
        updateCadView();
    }

    // 타깃에서 카메라를 향하는 임의 방향으로. 위쪽은 월드 -Y 기준으로 맞춘다
    // (정확히 위/아래를 볼 때는 +Z 를 위로).
    void setViewFromDirection(const vec3& dirFromTarget) {
        const vec3 f = normalize(dirFromTarget) * -1.0f;  // 카메라가 바라보는 방향
        vec3 upRef{0.0f, -1.0f, 0.0f};
        if (std::fabs(dot(f, upRef)) > 0.999f) upRef = vec3{0.0f, 0.0f, 1.0f};
        const vec3 right = normalize(cross(f, upRef));
        const vec3 up = cross(right, f);

        // 열 = 기저 벡터 (q * X = right, q * -Y = up 이므로 두 번째 열은 -up).
        mat4 r = mat4::identity();
        r.m[0][0] = right.x; r.m[0][1] = right.y; r.m[0][2] = right.z;
        r.m[1][0] = -up.x;   r.m[1][1] = -up.y;   r.m[1][2] = -up.z;
        r.m[2][0] = f.x;     r.m[2][1] = f.y;     r.m[2][2] = f.z;
        orbitRotation_ = normalize(quat::fromMatrix(r));
        currentViewType_ = CadViewType::Isometric;
        presetView_ = false;  // 임의 방향 - 등각 버튼을 켜지 않는다
        updateCadView();
    }

    void setTarget(const vec3& t) { target_ = t; updateCadView(); }
    const vec3& getTarget() const { return target_; }
    float getOrbitDistance() const { return orbitDistance_; }
    CadViewType getCurrentViewType() const { return currentViewType_; }
    // 표준 뷰(F/T/R/I)를 누른 뒤 아직 돌리지 않았으면 그 뷰 번호, 아니면 -1. 툴바 표시용.
    int presetViewIndex() const {
        return (isCadMode() && presetView_) ? static_cast<int>(currentViewType_) : -1;
    }

    // 궤도 상태로 뷰 행렬을 다시 만든다. 상태를 바꾸는 함수들이 알아서 부른다.
    void updateCadView() {
        const vec3 forward = rotate(orbitRotation_, vec3{0.0f, 0.0f, 1.0f});
        const vec3 up = rotate(orbitRotation_, vec3{0.0f, -1.0f, 0.0f});
        setViewTarget(target_ - forward * orbitDistance_, target_, up);
    }

    // fovY 는 라디안. aspect 는 창 크기가 바뀔 때마다 다시 넣어줘야 한다.
    void setPerspectiveProjection(float fovY, float aspect, float nearZ, float farZ) {
        projection_ = mat4::perspective(fovY, aspect, nearZ, farZ);
        orthographic_ = false;
        orthoHalfHeight_ = 0.0f;
    }

    // 직교 투영. halfHeight 는 화면 세로 절반에 담기는 월드 길이 - 곧 줌이다.
    // 원근에서는 앞으로 가면 커지지만 직교에서는 이 값을 줄여야 커진다.
    void setOrthographicProjection(float halfHeight, float aspect, float nearZ, float farZ) {
        const float halfWidth = halfHeight * aspect;
        // +Y 가 아래라 화면 위쪽이 -halfHeight 다
        projection_ = mat4::orthographic(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                         nearZ, farZ);
        orthographic_ = true;
        orthoHalfHeight_ = halfHeight;
    }

    bool isOrthographic() const { return orthographic_; }
    float getOrthoHalfHeight() const { return orthoHalfHeight_; }

    // 카메라 위치와 '바라보는 방향'
    void setViewDirection(const vec3& position, const vec3& direction,
                          const vec3& up = vec3{0.0f, -1.0f, 0.0f}) {
        view_ = mat4::view(position, direction, up);
    }

    // 카메라 위치와 '바라보는 지점'
    void setViewTarget(const vec3& position, const vec3& target,
                       const vec3& up = vec3{0.0f, -1.0f, 0.0f}) {
        view_ = mat4::lookAt(position, target, up);
    }

    // 위치 + 쿼터니언. 카메라를 게임 오브젝트처럼 다룰 때 쓴다 (1인칭 조작).
    //
    // 카메라 변환이 T * R 이므로 뷰는 R^T * T^-1 이다. R 이 직교라 전치가
    // 역행렬이므로, R 의 열 u/v/w 를 행에 넣기만 하면 된다.
    void setViewFromTransform(const vec3& position, const quat& rotation) {
        const mat4 r = rotation.toMat4();
        const vec3 u{r.m[0][0], r.m[0][1], r.m[0][2]};
        const vec3 v{r.m[1][0], r.m[1][1], r.m[1][2]};
        const vec3 w{r.m[2][0], r.m[2][1], r.m[2][2]};

        view_ = mat4::identity();
        view_.m[0][0] = u.x;  view_.m[1][0] = u.y;  view_.m[2][0] = u.z;
        view_.m[0][1] = v.x;  view_.m[1][1] = v.y;  view_.m[2][1] = v.z;
        view_.m[0][2] = w.x;  view_.m[1][2] = w.y;  view_.m[2][2] = w.z;
        view_.m[3][0] = -dot(u, position);
        view_.m[3][1] = -dot(v, position);
        view_.m[3][2] = -dot(w, position);
    }

    const mat4& getProjection() const { return projection_; }
    const mat4& getView() const { return view_; }

    // 카메라의 월드 위치. 뷰 행렬을 거꾸로 푼다.
    //
    // view = [R | t] 이고 t = -R * p 이므로 p = -R^T * t.
    // R 의 행이 u, v, w (열 우선이라 m[c][r] 의 r 고정이 행) 이고
    // t 는 m[3][0..2] 다. 회전은 직교라 역행렬 = 전치, 일반 역행렬이 필요 없다.
    vec3 getPosition() const {
        const vec3 u{view_.m[0][0], view_.m[1][0], view_.m[2][0]};
        const vec3 v{view_.m[0][1], view_.m[1][1], view_.m[2][1]};
        const vec3 w{view_.m[0][2], view_.m[1][2], view_.m[2][2]};
        const vec3 t{view_.m[3][0], view_.m[3][1], view_.m[3][2]};
        return (u * t.x + v * t.y + w * t.z) * -1.0f;
    }

    // 오브젝트마다 projection * view * model 을 계산하므로 앞의 둘은 미리 접어둔다.
    mat4 getProjectionView() const { return projection_ * view_; }

    // 뷰 행렬의 세 행 = 카메라의 오른쪽 / 아래 / 앞 방향 (월드).
    // (+Y 가 아래인 규약이라 두 번째가 '아래'다.)
    vec3 getRight() const { return vec3{view_.m[0][0], view_.m[1][0], view_.m[2][0]}; }
    vec3 getDown() const { return vec3{view_.m[0][1], view_.m[1][1], view_.m[2][1]}; }
    vec3 getForward() const { return vec3{view_.m[0][2], view_.m[1][2], view_.m[2][2]}; }

    // 월드 좌표를 캔버스 픽셀로. 카메라 뒤면 false.
    bool projectToScreen(const vec3& world, float width, float height,
                         float& px, float& py) const {
        // clip = P * V * world. 두 행렬을 곱하지 않고 뷰 -> 클립을 차례로 푼다.
        const vec3 v = transformPoint(view_, world);
        const float cx = projection_.m[0][0] * v.x + projection_.m[3][0] * 1.0f;
        const float cy = projection_.m[1][1] * v.y + projection_.m[3][1] * 1.0f;
        const float cw = projection_.m[2][3] * v.z + projection_.m[3][3];  // 원근 vz, 직교 1
        if (cw <= 1e-6f) return false;
        const float ndcX = cx / cw;
        const float ndcY = cy / cw;
        px = (ndcX + 1.0f) * 0.5f * width;
        py = (1.0f - ndcY) * 0.5f * height;  // NDC 는 위가 +1, 픽셀은 위가 0
        return true;
    }

    // 그 위치에서 화면 1 픽셀이 월드 몇 단위인가. 스냅 마커처럼 화면 크기가
    // 일정해야 하는 것에 쓴다. 기즈모의 거리 비례 스케일과 같은 원리다.
    float worldPerPixel(const vec3& at, float viewportHeight) const {
        if (orthographic_) {
            return 2.0f * orthoHalfHeight_ / viewportHeight;
        }
        // 원근: 거리 d 에서 화면 세로에 담기는 월드 길이 = 2 d tan(fov/2),
        // P11 = -1 / tan(fov/2) 이므로 tan(fov/2) = -1 / P11.
        const float d = dot(at - getPosition(), getForward());
        const float tanHalf = -1.0f / projection_.m[1][1];
        return 2.0f * std::fmax(d, 0.01f) * tanHalf / viewportHeight;
    }

private:
    static constexpr float kZoomBase = 0.92f;
    static constexpr float kMaxNotchesPerFrame = 3.0f;  // 스크롤이 몰려도 한 번에 3노치까지
    static constexpr float kDefaultOrbitDistance = 4.0f;
    static constexpr float kMinOrbitDistance = 0.3f;
    static constexpr float kMaxOrbitDistance = 60.0f;
    static constexpr float kMinUpDot = 0.02f;  // 이보다 기울면 극점 - pitch 를 막는다

    mat4 projection_ = mat4::identity();
    mat4 view_ = mat4::identity();
    bool orthographic_ = false;
    float orthoHalfHeight_ = 0.0f;

    ViewMode viewMode_ = ViewMode::Cad;
    quat orbitRotation_{};
    vec3 target_{0.0f, 0.0f, 0.0f};
    float orbitDistance_ = kDefaultOrbitDistance;
    float maxOrbitDistance_ = kMaxOrbitDistance;  // frame() 이 씬 크기에 맞춰 늘린다
    CadViewType currentViewType_ = CadViewType::Front;
    bool presetView_ = false;
};
