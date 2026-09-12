#pragma once

#include "lot_math.h"
#include <memory>
#include <unordered_map>

class LotModel;
class LotMaterial;

// 게임 오브젝트 클래스
class LotGameObject {
public:
    using id_t = unsigned int;

    // 장면의 오브젝트 목록. vector 가 아니라 맵인 이유:
    // 피킹/선택은 "무엇을 골랐는지"를 들고 있어야 하는데, vector 인덱스는
    // 중간에 하나가 지워지면 밀려서 엉뚱한 것을 가리킨다. id 는 안 바뀐다.
    using Map = std::unordered_map<id_t, LotGameObject>;

    // 없는 id 면 nullptr. 선택된 오브젝트가 지워졌을 때를 위한 안전한 조회.
    static LotGameObject* find(Map& map, id_t id) {
        auto it = map.find(id);
        return (it == map.end()) ? nullptr : &it->second;
    }

    // 팩토리 메서드로 생성. id 는 0 부터 한 번씩만 나간다.
    static LotGameObject createGameObject() {
        static id_t currentId = 0;
        return LotGameObject{currentId++};
    }

    // "선택 없음" 을 나타내는 값. 실제 id 로는 절대 나오지 않는다.
    static constexpr id_t kInvalidId = ~static_cast<id_t>(0);

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

    // 재질 (텍스처). 없으면 렌더 시스템의 기본 재질(흰색)을 쓴다.
    std::shared_ptr<LotMaterial> material;

private:
    explicit LotGameObject(id_t objId) : id_(objId) {}

    id_t id_;
};
