#pragma once

#include "lot_math.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// 도면층 (레이어).
//
// 오브젝트마다 layer 하나. 층을 끄면 안 보이고 (피킹/스냅에서도 빠진다), 잠그면
// 보이되 골라지지 않는다. 층 색은 오브젝트 색이 '층 따름'(ByLayer)일 때 쓰인다.
// Vulkan 쪽 LotLayer / LotLayers 와 같은 정의고 .lot 의 "layers" 배열 + 오브젝트의
// "layer" id 로 오간다.
//
// id 0 은 항상 있는 기본층("0") - 지울 수도, 끌 수도, 잠글 수도 없다 (AutoCAD 와 같다).
// 새 오브젝트는 '현재 층'에 들어간다.
class LotLayers {
public:
    struct Layer {
        uint32_t id = 0;
        std::string name;
        bool visible = true;
        bool locked = false;
        vec3 color{0.8f, 0.8f, 0.85f};
        uint32_t linetype = 0;  // lot_linetype::kContinuous
        float opacity = 1.0f;
    };

    static constexpr uint32_t kDefault = 0;

    LotLayers();

    // 층 만들기. 이름이 비었거나 겹치면 뒤에 번호를 붙인다. 새 id 반환.
    uint32_t create(const std::string& name, const vec3& color);

    // 없으면 nullptr. id 0 은 항상 있다.
    const Layer* find(uint32_t id) const;
    Layer* find(uint32_t id);

    // id 순서대로 (0 이 먼저).
    std::vector<const Layer*> all() const;
    size_t size() const { return layers_.size(); }

    // 층을 지운다 (0 은 못 지운다). 그 층의 오브젝트는 호출자가 옮긴다.
    bool remove(uint32_t id);

    // 보이나 / 고를 수 있나. 없는 층은 보이고 고를 수 있는 것으로 본다
    // (파일이 깨져 층을 잃어도 오브젝트가 사라지지 않게).
    bool isVisible(uint32_t id) const;
    bool isSelectable(uint32_t id) const;

    // 현재 층 - 새로 만드는 오브젝트가 들어간다.
    uint32_t current() const { return current_; }
    void setCurrent(uint32_t id) { if (find(id)) current_ = id; }

    void clear();

private:
    std::map<uint32_t, Layer> layers_;
    uint32_t next_ = 1;
    uint32_t current_ = kDefault;
};
