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

    // 직교 투영 행렬. CAD 도면 뷰처럼 거리에 따라 크기가 변하지 않는다.
    //
    // 인자는 '뷰 공간' 값이다. +Y 가 아래인 규약이라 화면 위쪽에 보일 y 값이
    // 음수다 - 그래서 top < bottom 으로 넘어온다 (예: top = -h, bottom = +h).
    // 원근과 마찬가지로 WebGPU 클립 공간(+Y 위, 깊이 [0, 1])에 맞춘다.
    static mat4 orthographic(float left, float right, float top, float bottom,
                             float nearZ, float farZ) {
        mat4 result;
        result.m[0][0] = 2.0f / (right - left);
        result.m[1][1] = 2.0f / (top - bottom);   // top 이 음수라 결과적으로 Y 가 뒤집힌다
        result.m[2][2] = 1.0f / (farZ - nearZ);
        result.m[3][0] = -(right + left) / (right - left);
        result.m[3][1] = -(top + bottom) / (top - bottom);
        result.m[3][2] = -nearZ / (farZ - nearZ);
        result.m[3][3] = 1.0f;                     // w = 1: 원근 나눗셈이 없다
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

// 점 변환 (w = 1). 이동이 적용된다.
inline vec3 transformPoint(const mat4& m, const vec3& p) {
    return vec3{
        m.m[0][0] * p.x + m.m[1][0] * p.y + m.m[2][0] * p.z + m.m[3][0],
        m.m[0][1] * p.x + m.m[1][1] * p.y + m.m[2][1] * p.z + m.m[3][1],
        m.m[0][2] * p.x + m.m[1][2] * p.y + m.m[2][2] * p.z + m.m[3][2],
    };
}

// 쿼터니언 (w, x, y, z) - 회전 표현.
//
// 오일러 각을 버리고 이걸 쓰는 이유: 회전 기즈모로 임의 축 둘레를 계속 돌리면
// 오일러는 짐벌락(cos(x) = 0 근처에서 y, z 가 한 자유도로 겹침)에 걸리고
// 행렬 -> 오일러 복원 때 값이 튄다. 쿼터니언은 합성이 곱 하나고 특이점이 없다.
// Vulkan 쪽(glm::quat)과 같은 규약이라 값이 그대로 옮겨진다.
struct quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    quat() = default;
    quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

    static quat identity() { return quat{}; }

    // 단위 축 둘레 angle 라디안 회전 (glm::angleAxis 와 같다).
    static quat angleAxis(float angle, const vec3& axis) {
        const float h = angle * 0.5f;
        const float s = std::sin(h);
        return quat{std::cos(h), axis.x * s, axis.y * s, axis.z * s};
    }

    // Tait-Bryan 각 (Y -> X -> Z, 즉 R = Ry * Rx * Rz) 에서. 예전 TransformComponent
    // 의 rotation 값을 그대로 넘기면 같은 자세가 된다 - 초기 배치 코드 호환용.
    static quat fromEulerYXZ(const vec3& e) {
        const quat qy = angleAxis(e.y, vec3{0.0f, 1.0f, 0.0f});
        const quat qx = angleAxis(e.x, vec3{1.0f, 0.0f, 0.0f});
        const quat qz = angleAxis(e.z, vec3{0.0f, 0.0f, 1.0f});
        return qy * qx * qz;
    }

    // 회전 행렬(직교, 스케일 없음)에서. Shepperd 방식 - 대각 성분 중 가장 큰
    // 것을 기준으로 뽑아 나눗셈이 0 근처가 되는 것을 피한다.
    static quat fromMatrix(const mat4& m) {
        // R(r, c) = m[c][r]
        const float r00 = m.m[0][0], r01 = m.m[1][0], r02 = m.m[2][0];
        const float r10 = m.m[0][1], r11 = m.m[1][1], r12 = m.m[2][1];
        const float r20 = m.m[0][2], r21 = m.m[1][2], r22 = m.m[2][2];
        const float trace = r00 + r11 + r22;
        quat q;
        if (trace > 0.0f) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (r21 - r12) / s;
            q.y = (r02 - r20) / s;
            q.z = (r10 - r01) / s;
        } else if (r00 > r11 && r00 > r22) {
            const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
            q.w = (r21 - r12) / s;
            q.x = 0.25f * s;
            q.y = (r01 + r10) / s;
            q.z = (r02 + r20) / s;
        } else if (r11 > r22) {
            const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
            q.w = (r02 - r20) / s;
            q.x = (r01 + r10) / s;
            q.y = 0.25f * s;
            q.z = (r12 + r21) / s;
        } else {
            const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
            q.w = (r10 - r01) / s;
            q.x = (r02 + r20) / s;
            q.y = (r12 + r21) / s;
            q.z = 0.25f * s;
        }
        return q;
    }

    // 합성: (a * b) 는 b 를 먼저, a 를 나중에 적용한다 (행렬 곱과 같은 순서).
    quat operator*(const quat& b) const {
        return quat{
            w * b.w - x * b.x - y * b.y - z * b.z,
            w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y - x * b.z + y * b.w + z * b.x,
            w * b.z + x * b.y - y * b.x + z * b.w,
        };
    }

    // 역회전. 단위 쿼터니언이면 역원 = 공액.
    quat conjugate() const { return quat{w, -x, -y, -z}; }

    // 회전 행렬 (열 우선). 상단 3x3 만 채우고 나머지는 단위.
    mat4 toMat4() const {
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;

        mat4 r = mat4::identity();
        // m[col][row]
        r.m[0][0] = 1.0f - 2.0f * (yy + zz);
        r.m[0][1] = 2.0f * (xy + wz);
        r.m[0][2] = 2.0f * (xz - wy);

        r.m[1][0] = 2.0f * (xy - wz);
        r.m[1][1] = 1.0f - 2.0f * (xx + zz);
        r.m[1][2] = 2.0f * (yz + wx);

        r.m[2][0] = 2.0f * (xz + wy);
        r.m[2][1] = 2.0f * (yz - wx);
        r.m[2][2] = 1.0f - 2.0f * (xx + yy);
        return r;
    }

    // 다시 Tait-Bryan 각으로 (Y -> X -> Z). 로그/디버그용 - 편집 계산에는 쓰지 않는다.
    //
    // R = Ry * Rx * Rz 를 펼치면 m[2][1] = -sin(x) 라 x 가 바로 나오고,
    // cos(x) != 0 이면 m[2][0] / m[2][2] = tan(y), m[0][1] / m[1][1] = tan(z).
    // cos(x) = 0 이면 y 와 z 가 겹치므로 z = 0 으로 두고 y 를 첫 열에서 읽는다.
    vec3 toEulerYXZ() const {
        const mat4 r = toMat4();
        float sx = -r.m[2][1];
        if (sx > 1.0f) sx = 1.0f;
        if (sx < -1.0f) sx = -1.0f;
        vec3 e;
        e.x = std::asin(sx);
        const float cx = std::cos(e.x);
        if (std::fabs(cx) > 1e-4f) {
            e.y = std::atan2(r.m[2][0], r.m[2][2]);
            e.z = std::atan2(r.m[0][1], r.m[1][1]);
        } else {
            e.z = 0.0f;
            e.y = std::atan2(-r.m[0][2], r.m[0][0]);
        }
        return e;
    }
};

inline float dot(const quat& a, const quat& b) {
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

// 곱을 반복하면 길이가 1 에서 조금씩 벗어난다. 편집 한 번마다 한 번 불러준다.
inline quat normalize(const quat& q) {
    const float len = std::sqrt(dot(q, q));
    if (len <= 0.0f) return quat::identity();
    const float inv = 1.0f / len;
    return quat{q.w * inv, q.x * inv, q.y * inv, q.z * inv};
}

// 벡터 회전 v' = q v q*. 행렬을 만들지 않고 직접 푼 형태.
inline vec3 rotate(const quat& q, const vec3& v) {
    const vec3 u{q.x, q.y, q.z};
    const vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

// 3D 변환 컴포넌트
//
// model = T * R * S. 회전은 쿼터니언이다 (오일러 금지 - 위 quat 주석 참고).
// 오일러로 자세를 주고 싶으면 setRotationEuler 를 거친다.
struct TransformComponent {
    vec3 translation{0.0f, 0.0f, 0.0f};
    vec3 scale{1.0f, 1.0f, 1.0f};
    quat rotation{};

    // R 의 열에 스케일을 곱하고 마지막 열에 이동을 넣는다.
    mat4 mat4Transform() const {
        mat4 result = rotation.toMat4();
        for (int r = 0; r < 3; ++r) {
            result.m[0][r] *= scale.x;
            result.m[1][r] *= scale.y;
            result.m[2][r] *= scale.z;
        }
        result.m[3][0] = translation.x;
        result.m[3][1] = translation.y;
        result.m[3][2] = translation.z;
        return result;
    }

    // 노멀 변환 행렬 = transpose(inverse(mat3(model))).
    //
    // 모델 행렬을 노멀에 그대로 쓰면 안 된다. 스케일이 축마다 다를 때
    // (예: scale{2, 1, 1}) 노멀이 면에 수직이 아니게 기울어지기 때문이다.
    // 회전은 직교행렬이라 역전치가 자기 자신이고, 스케일만 역수를 취하면
    // 되므로 mat4Transform 의 스케일 자리에 1/scale 을 넣은 것과 같다.
    //
    // 상단 3x3 만 의미가 있다. 셰이더에서는 vec4(normal, 0) 을 곱해 쓴다.
    mat4 normalMatrix() const {
        mat4 result = rotation.toMat4();
        for (int r = 0; r < 3; ++r) {
            result.m[0][r] /= scale.x;
            result.m[1][r] /= scale.y;
            result.m[2][r] /= scale.z;
        }
        return result;
    }

    // 회전만 (스케일/이동 없음).
    mat4 rotationMatrix() const { return rotation.toMat4(); }

    // Tait-Bryan 각(Y -> X -> Z, 라디안)으로 자세 지정. 초기 배치처럼
    // 사람이 숫자를 적는 자리용이다.
    void setRotationEuler(const vec3& e) { rotation = quat::fromEulerYXZ(e); }
    vec3 eulerAngles() const { return rotation.toEulerYXZ(); }

    // 월드 축 둘레로 angle 만큼 더 돌린다 (기즈모: 오브젝트 자기 회전 뒤에 월드 회전).
    void rotateWorld(float angle, const vec3& axis) {
        rotation = normalize(quat::angleAxis(angle, axis) * rotation);
    }
    // 자기 축 둘레로 (Vulkan 쪽 rotateAroundAxis 와 같은 순서).
    void rotateLocal(float angle, const vec3& axis) {
        rotation = normalize(rotation * quat::angleAxis(angle, axis));
    }

    // 회전 행렬에서 (스케일 없는 직교 행렬이어야 한다).
    void setRotationFromMatrix(const mat4& r) { rotation = normalize(quat::fromMatrix(r)); }

    // 월드 -> 로컬 (방향). 피킹에서 레이를 모델 공간으로 가져올 때 쓴다.
    //
    // model = T * R * S 이므로 역은 S^-1 * R^T * T^-1 이다. 일반 역행렬을
    // 구하지 않아도 된다: model 의 열 c_i = R 의 열 * s_i 이고 R 은 직교라
    //   (R^T v)_i = dot(R열_i, v) = dot(c_i, v) / s_i
    // 여기에 S^-1 로 한 번 더 나누면 dot(c_i, v) / s_i^2 이 된다.
    vec3 worldToLocalDirection(const vec3& d) const {
        const mat4 m = mat4Transform();
        const vec3 c0{m.m[0][0], m.m[0][1], m.m[0][2]};
        const vec3 c1{m.m[1][0], m.m[1][1], m.m[1][2]};
        const vec3 c2{m.m[2][0], m.m[2][1], m.m[2][2]};
        return vec3{
            dot(c0, d) / (scale.x * scale.x),
            dot(c1, d) / (scale.y * scale.y),
            dot(c2, d) / (scale.z * scale.z),
        };
    }

    // 월드 -> 로컬 (점). 이동을 먼저 벗긴다.
    vec3 worldToLocalPoint(const vec3& p) const {
        return worldToLocalDirection(p - translation);
    }
};
