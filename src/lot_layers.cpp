#include "lot_layers.h"
#include "lot_log.h"

LotLayers::LotLayers() { clear(); }

void LotLayers::clear() {
    layers_.clear();
    next_ = 1;
    current_ = kDefault;
    Layer zero;
    zero.id = kDefault;
    zero.name = "0";
    layers_.emplace(kDefault, std::move(zero));
}

uint32_t LotLayers::create(const std::string& name, const vec3& color) {
    std::string base = name.empty() ? "Layer" : name;
    if (base.size() > 200) base.resize(200);
    std::string unique = base;
    for (int suffix = 1;; ++suffix) {
        bool taken = false;
        for (const auto& entry : layers_) {
            if (entry.second.name == unique) { taken = true; break; }
        }
        if (!taken) break;
        unique = base + " (" + std::to_string(suffix) + ")";
    }

    Layer layer;
    layer.id = next_++;
    layer.name = unique;
    layer.color = color;
    const uint32_t id = layer.id;
    layers_.emplace(id, std::move(layer));
    LOT_LOG("layer: created " << id << " \"" << unique << "\"");
    return id;
}

const LotLayers::Layer* LotLayers::find(uint32_t id) const {
    auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : &it->second;
}

LotLayers::Layer* LotLayers::find(uint32_t id) {
    auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : &it->second;
}

std::vector<const LotLayers::Layer*> LotLayers::all() const {
    std::vector<const Layer*> out;
    out.reserve(layers_.size());
    for (const auto& entry : layers_) out.push_back(&entry.second);
    return out;  // std::map 이라 id 순서
}

bool LotLayers::remove(uint32_t id) {
    if (id == kDefault) return false;
    if (layers_.erase(id) == 0) return false;
    if (current_ == id) current_ = kDefault;
    LOT_LOG("layer: removed " << id);
    return true;
}

bool LotLayers::isVisible(uint32_t id) const {
    const Layer* l = find(id);
    return l == nullptr || l->visible;
}

bool LotLayers::isSelectable(uint32_t id) const {
    const Layer* l = find(id);
    return l == nullptr || (l->visible && !l->locked);
}
