#pragma once

#include <cmath>

// 간단한 2D 벡터
struct vec2 {
    float x = 0.0f;
    float y = 0.0f;

    vec2() = default;
    vec2(float x_, float y_) : x(x_), y(y_) {}
};

// 간단한 3D 벡터
struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    vec3() = default;
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
};

inline vec3 operator+(const vec3& a, const vec3& b) {
    return vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

inline vec3 operator-(const vec3& a, const vec3& b) {
    return vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

inline vec3 operator*(const vec3& v, float s) {
    return vec3{v.x * s, v.y * s, v.z * s};
}

inline float dot(const vec3& a, const vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline vec3 cross(const vec3& a, const vec3& b) {
    return vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline vec3 normalize(const vec3& v) {
    const float len = std::sqrt(dot(v, v));
    if (len <= 0.0f) return v;
    return v * (1.0f / len);
}

// 4x4 행렬 (열 우선 - WGSL 의 mat4x4<f32> 와 같은 메모리 배치)
//
// m[c][r] = c 번째 열, r 번째 행.
// 셰이더로 그대로 memcpy 할 수 있어야 하므로 열 우선을 지킨다.
struct mat4 {
    float m[4][4]{};

    mat4() = default;

    // 대각 행렬 (1.0f 을 주면 단위 행렬)
    explicit mat4(float diagonal) {
        m[0][0] = diagonal;
        m[1][1] = diagonal;
        m[2][2] = diagonal;
        m[3][3] = diagonal;
    }

    static mat4 identity() { return mat4{1.0f}; }

    mat4 operator*(const mat4& other) const {
        mat4 result;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k][r] * other.m[c][k];
                }
                result.m[c][r] = sum;
            }
        }
        return result;
    }

    // 원근 투영 행렬.
    //
    // 좌표 규약은 Vulkan 쪽 원본과 같다: +X 오른쪽, +Y 아래, +Z 화면 안쪽.
    // 깊이 범위도 Vulkan 과 같은 [0, 1] 이라 공식이 그대로 옮겨진다.
    // 딱 한 군데만 다르다 - WebGPU 는 클립 공간의 +Y 가 위쪽이므로
    // m[1][1] 의 부호를 뒤집어 여기서 한 번에 맞춘다.
    static mat4 perspective(float fovY, float aspect, float nearZ, float farZ) {
        const float tanHalfFovY = std::tan(fovY / 2.0f);

        mat4 result;  // 전부 0 - 원근 투영은 단위 행렬에서 출발하지 않는다
        result.m[0][0] = 1.0f / (aspect * tanHalfFovY);
        result.m[1][1] = -1.0f / tanHalfFovY;
        result.m[2][2] = farZ / (farZ - nearZ);
        result.m[2][3] = 1.0f;
        result.m[3][2] = -(farZ * nearZ) / (farZ - nearZ);
        return result;
    }

    // 뷰 행렬 (카메라를 원점으로 옮기는 변환).
    //
    // up 기본값이 {0, -1, 0} 인 이유는 이 엔진에서 +Y 가 아래이기 때문이다.
    static mat4 view(const vec3& position, const vec3& direction,
                     const vec3& up = vec3{0.0f, -1.0f, 0.0f}) {
        const vec3 w = normalize(direction);
        const vec3 u = normalize(cross(w, up));
        const vec3 v = cross(w, u);

        mat4 result = identity();
        result.m[0][0] = u.x;  result.m[1][0] = u.y;  result.m[2][0] = u.z;
        result.m[0][1] = v.x;  result.m[1][1] = v.y;  result.m[2][1] = v.z;
        result.m[0][2] = w.x;  result.m[1][2] = w.y;  result.m[2][2] = w.z;
        result.m[3][0] = -dot(u, position);
        result.m[3][1] = -dot(v, position);
        result.m[3][2] = -dot(w, position);
        return result;
    }

    static mat4 lookAt(const vec3& position, const vec3& target,
                       const vec3& up = vec3{0.0f, -1.0f, 0.0f}) {
        return view(position, target - position, up);
    }
};

// 3D 변환 컴포넌트
//
// 회전은 Tait-Bryan 각(Y -> X -> Z 순서)을 쓴다.
// Vulkan 쪽 원본과 같은 규약이라 값이 그대로 옮겨진다.
struct TransformComponent {
    vec3 translation{0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f, 1.0f, 1.0f};
    vec3 rotation{0.0f, 0.0f, 0.0f};  // 라디안

    // translate * Ry * Rx * Rz * scale 을 펼쳐 쓴 것.
    // 행렬 곱을 매 프레임 4번 하는 대신 결과를 직접 채운다.
    mat4 mat4Transform() const {
        const float c3 = std::cos(rotation.z);
        const float s3 = std::sin(rotation.z);
        const float c2 = std::cos(rotation.x);
        const float s2 = std::sin(rotation.x);
        const float c1 = std::cos(rotation.y);
        const float s1 = std::sin(rotation.y);

        mat4 result;
        result.m[0][0] = scale.x * (c1 * c3 + s1 * s2 * s3);
        result.m[0][1] = scale.x * (c2 * s3);
        result.m[0][2] = scale.x * (c1 * s2 * s3 - c3 * s1);
        result.m[0][3] = 0.0f;

        result.m[1][0] = scale.y * (c3 * s1 * s2 - c1 * s3);
        result.m[1][1] = scale.y * (c2 * c3);
        result.m[1][2] = scale.y * (c1 * c3 * s2 + s1 * s3);
        result.m[1][3] = 0.0f;

        result.m[2][0] = scale.z * (c2 * s1);
        result.m[2][1] = scale.z * (-s2);
        result.m[2][2] = scale.z * (c1 * c2);
        result.m[2][3] = 0.0f;

        result.m[3][0] = translation.x;
        result.m[3][1] = translation.y;
        result.m[3][2] = translation.z;
        result.m[3][3] = 1.0f;
        return result;
    }

    // 노멀 변환 행렬 = transpose(inverse(mat3(model))).
    //
    // 모델 행렬을 노멀에 그대로 쓰면 안 된다. 스케일이 축마다 다를 때
    // (예: scale{2, 1, 1}) 노멀이 면에 수직이 아니게 기울어지기 때문이다.
    // 회전은 직교행렬이라 역전치가 자기 자신이고, 스케일만 역수를 취하면
    // 되므로 일반적인 역행렬 계산 없이 mat4Transform 의 스케일 자리에
    // 1/scale 을 넣은 것과 같다.
    //
    // 상단 3x3 만 의미가 있다. 셰이더에서는 vec4(normal, 0) 을 곱해 쓴다.
    mat4 normalMatrix() const {
        const float c3 = std::cos(rotation.z);
        const float s3 = std::sin(rotation.z);
        const float c2 = std::cos(rotation.x);
        const float s2 = std::sin(rotation.x);
        const float c1 = std::cos(rotation.y);
        const float s1 = std::sin(rotation.y);

        const vec3 invScale{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};

        mat4 result;
        result.m[0][0] = invScale.x * (c1 * c3 + s1 * s2 * s3);
        result.m[0][1] = invScale.x * (c2 * s3);
        result.m[0][2] = invScale.x * (c1 * s2 * s3 - c3 * s1);

        result.m[1][0] = invScale.y * (c3 * s1 * s2 - c1 * s3);
        result.m[1][1] = invScale.y * (c2 * c3);
        result.m[1][2] = invScale.y * (c1 * c3 * s2 + s1 * s3);

        result.m[2][0] = invScale.z * (c2 * s1);
        result.m[2][1] = invScale.z * (-s2);
        result.m[2][2] = invScale.z * (c1 * c2);

        result.m[3][3] = 1.0f;
        return result;
    }
};
