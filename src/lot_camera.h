#pragma once

#include "lot_math.h"

// 카메라 - 투영 행렬과 뷰 행렬만 들고 있는 순수 계산 클래스.
// GPU 리소스를 잡지 않으므로 헤더 하나로 끝난다.
class LotCamera {
public:
    // fovY 는 라디안. aspect 는 창 크기가 바뀔 때마다 다시 넣어줘야 한다.
    void setPerspectiveProjection(float fovY, float aspect, float nearZ, float farZ) {
        projection_ = mat4::perspective(fovY, aspect, nearZ, farZ);
    }

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

    // 오브젝트마다 projection * view * model 을 계산하므로 앞의 둘은 미리 접어둔다.
    mat4 getProjectionView() const { return projection_ * view_; }

private:
    mat4 projection_ = mat4::identity();
    mat4 view_ = mat4::identity();
};
