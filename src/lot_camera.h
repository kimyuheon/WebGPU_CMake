#pragma once

#include "lot_math.h"

// 카메라 - 투영 행렬과 뷰 행렬만 들고 있는 순수 계산 클래스.
// GPU 리소스를 잡지 않으므로 헤더 하나로 끝난다.
class LotCamera {
public:
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

    // 위치 + 오일러 각. 카메라를 게임 오브젝트처럼 다룰 때 쓴다 (1인칭 조작).
    void setViewYXZ(const vec3& position, const vec3& rotation) {
        view_ = mat4::viewYXZ(position, rotation);
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

private:
    mat4 projection_ = mat4::identity();
    mat4 view_ = mat4::identity();
    bool orthographic_ = false;
    float orthoHalfHeight_ = 0.0f;
};
