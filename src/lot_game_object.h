#pragma once

#include <memory>
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
};

// 2x2 행렬
struct mat2 {
    float m[2][2] = {{1, 0}, {0, 1}};

    mat2() = default;
    mat2(float m00, float m01, float m10, float m11) {
        m[0][0] = m00; m[0][1] = m01;
        m[1][0] = m10; m[1][1] = m11;
    }

    // 행렬 곱셈
    mat2 operator*(const mat2& other) const {
        mat2 result;
        result.m[0][0] = m[0][0] * other.m[0][0] + m[0][1] * other.m[1][0];
        result.m[0][1] = m[0][0] * other.m[0][1] + m[0][1] * other.m[1][1];
        result.m[1][0] = m[1][0] * other.m[0][0] + m[1][1] * other.m[1][0];
        result.m[1][1] = m[1][0] * other.m[0][1] + m[1][1] * other.m[1][1];
        return result;
    }
};

// 2D 변환 컴포넌트
struct Transform2DComponent {
    vec2 translation{0.0f, 0.0f};
    vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;  // 라디안

    // 변환 행렬 계산 (회전 * 스케일)
    mat2 mat2Transform() const {
        const float s = std::sin(rotation);
        const float c = std::cos(rotation);

        // 회전 행렬
        mat2 rotMatrix(c, s, -s, c);

        // 스케일 행렬
        mat2 scaleMatrix(scale.x, 0.0f, 0.0f, scale.y);

        return rotMatrix * scaleMatrix;
    }
};

// 게임 오브젝트 클래스
class LotGameObject {
public:
    using id_t = unsigned int;

    // 팩토리 메서드로 생성
    static LotGameObject createGameObject() {
        static id_t currentId = 0;
        return LotGameObject{currentId++};
    }

    // 복사 금지, 이동 허용
    LotGameObject(const LotGameObject&) = delete;
    LotGameObject& operator=(const LotGameObject&) = delete;
    LotGameObject(LotGameObject&&) = default;
    LotGameObject& operator=(LotGameObject&&) = default;

    id_t getId() const { return id_; }

    // 공개 멤버 (간단한 구조를 위해)
    vec3 color{1.0f, 1.0f, 1.0f};
    Transform2DComponent transform2d{};

    // 모델 정보 (vertex count, buffer ID 등)
    int vertexCount = 0;
    int modelBufferId = -1;  // JavaScript에서 관리하는 버퍼 ID

private:
    explicit LotGameObject(id_t objId) : id_(objId) {}

    id_t id_;
};
