#pragma once

#include "lot_math.h"
#include <memory>

class LotModel;

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
    TransformComponent transform{};

    // 모델.
    // 여러 오브젝트가 같은 메시(예: 큐브 하나)를 공유하므로 shared_ptr 이다.
    // 정점 개수는 모델이 알고 있으니 여기서 다시 세지 않는다.
    std::shared_ptr<LotModel> model;

private:
    explicit LotGameObject(id_t objId) : id_(objId) {}

    id_t id_;
};
