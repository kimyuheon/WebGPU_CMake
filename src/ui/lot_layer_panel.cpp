#include "lot_layer_panel.h"
#include "lot_linetype.h"
#include "lot_log.h"
#include "lot_ui_command.h"

#include <cstdio>
#include <set>

namespace lot_ui {
namespace {

std::string hexColor(const vec3& c) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\"#%02x%02x%02x\"",
                  static_cast<int>(c.x * 255.0f + 0.5f),
                  static_cast<int>(c.y * 255.0f + 0.5f),
                  static_cast<int>(c.z * 255.0f + 0.5f));
    return buf;
}

int packColor(const vec3& c) {
    return (static_cast<int>(c.x * 255.0f + 0.5f) << 16)
         | (static_cast<int>(c.y * 255.0f + 0.5f) << 8)
         | static_cast<int>(c.z * 255.0f + 0.5f);
}

vec3 unpackColor(int rgb) {
    return vec3{((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f};
}

}  // namespace

std::string LotLayerPanel::diffJson(const LotLayers& layers, const LotGameObject::Map& objects,
                                    const EditController& edit,
                                    const std::unordered_map<uint32_t, int>& counts) {
    // 선택 상태: 전부 같으면 그 값, 섞였으면 -2, 선택이 없으면 -3 (패널이 감춘다)
    int selLt = -3, selColor = -3, selByLayer = -3;
    for (LotGameObject::id_t sel : edit.selection()) {
        const auto* obj = LotGameObject::find(objects, sel);
        if (!obj) continue;
        const int lt = (obj->linetype == lot_linetype::kByLayer) ? -1 : static_cast<int>(obj->linetype);
        if (selLt == -3) selLt = lt; else if (selLt != lt) selLt = -2;
        const int rgb = packColor(obj->color);
        if (selColor == -3) selColor = rgb; else if (selColor != rgb) selColor = -2;
        const int bl = obj->colorByLayer ? 1 : 0;
        if (selByLayer == -3) selByLayer = bl; else if (selByLayer != bl) selByLayer = -2;
    }

    std::string j = "{\"current\":" + std::to_string(layers.current())
                  + ",\"selectionLinetype\":" + std::to_string(selLt)
                  + ",\"selectionColor\":" + std::to_string(selColor)
                  + ",\"selectionByLayer\":" + std::to_string(selByLayer) + ",\"layers\":[";
    bool first = true;
    for (const LotLayers::Layer* l : layers.all()) {
        if (!first) j += ",";
        first = false;
        // 예전에는 층마다 객체를 전부 셌다 (층 수 x 객체 수, 큰 도면에서 프레임당 수백만 번)
        const auto c = counts.find(l->id);
        const int count = (c == counts.end()) ? 0 : c->second;
        j += "{\"id\":" + std::to_string(l->id) + ",\"name\":" + quote(l->name)
           + ",\"visible\":" + (l->visible ? "true" : "false")
           + ",\"locked\":" + (l->locked ? "true" : "false")
           + ",\"linetype\":" + std::to_string(l->linetype)
           + ",\"color\":" + hexColor(l->color)
           + ",\"count\":" + std::to_string(count) + "}";
    }
    j += "]}";

    if (j == last_) return std::string();
    last_ = j;
    return j;
}

bool LotLayerPanel::command(const std::string& what, uint32_t id, int value,
                            LotLayers& layers, LotGameObject::Map& objects, EditController& edit) {
    // 선택 전체에 같은 속성을 먹이는 편집 - 히스토리 한 덩어리로 묶는다
    auto editSelection = [&](const char* label, void (*apply)(LotGameObject&, int), int arg) {
        if (!edit.hasSelection()) return false;
        EditHistory::Edit e;
        e.label = label;
        e.before = EditHistory::snapshot(objects, edit.selection());
        for (LotGameObject::id_t sel : edit.selection()) {
            if (auto* obj = LotGameObject::find(objects, sel)) apply(*obj, arg);
        }
        e.after = EditHistory::snapshot(objects, edit.selection());
        edit.history().record(std::move(e));
        return true;
    };

    if (what == "new") {
        // 새 층 색은 돌아가며 고른다 - 손으로 고르는 자리는 만든 뒤 색 견본에서
        static const vec3 kPalette[] = {
            {0.95f, 0.45f, 0.45f}, {0.45f, 0.85f, 0.55f}, {0.45f, 0.65f, 0.95f},
            {0.95f, 0.8f, 0.4f},   {0.8f, 0.55f, 0.95f},  {0.4f, 0.9f, 0.9f},
        };
        const size_t n = layers.size() - 1;  // 기본층 빼고
        const uint32_t newId = layers.create("Layer " + std::to_string(n + 1),
                                             kPalette[n % (sizeof(kPalette) / sizeof(kPalette[0]))]);
        layers.setCurrent(newId);
        return true;
    }
    if (what == "current") {
        layers.setCurrent(id);
        LOT_LOG("layer: current = " << layers.current());
        return true;
    }
    if (what == "visible" || what == "locked") {
        LotLayers::Layer* l = layers.find(id);
        if (!l || id == LotLayers::kDefault) return false;
        (what == "visible" ? l->visible : l->locked) = (value != 0);
        LOT_LOG("layer: " << l->name << " " << what << " = " << (value != 0 ? "on" : "off"));
        if (!layers.isSelectable(id)) {
            // 안 보이거나 잠긴 층의 오브젝트는 선택에서 뺀다
            std::set<LotGameObject::id_t> keep;
            for (LotGameObject::id_t sel : edit.selection()) {
                const auto* obj = LotGameObject::find(objects, sel);
                if (obj && obj->layer != id) keep.insert(sel);
            }
            edit.setSelection(std::move(keep));
        }
        return true;
    }
    if (what == "layerColor") {
        if (LotLayers::Layer* l = layers.find(id)) {
            l->color = unpackColor(value);
            LOT_LOG("layer: " << l->name << " color set");
            return true;
        }
        return false;
    }
    if (what == "layerLinetype") {
        if (LotLayers::Layer* l = layers.find(id)) {
            l->linetype = static_cast<uint32_t>(value);
            LOT_LOG("layer: " << l->name << " linetype = " << lot_linetype::name(l->linetype));
            return true;
        }
        return false;
    }
    if (what == "assign") {
        if (!layers.find(id)) return false;
        const bool ok = editSelection("layer", [](LotGameObject& o, int v) {
            o.layer = static_cast<uint32_t>(v);
        }, static_cast<int>(id));
        if (ok) LOT_LOG("layer: moved " << edit.selection().size() << " objects to layer " << id);
        return ok;
    }
    if (what == "objectLinetype") {
        const bool ok = editSelection("linetype", [](LotGameObject& o, int v) {
            o.linetype = (v < 0) ? lot_linetype::kByLayer : static_cast<uint32_t>(v);
        }, value);
        if (ok) LOT_LOG("linetype: selection -> "
                        << lot_linetype::name(value < 0 ? lot_linetype::kByLayer
                                                        : static_cast<uint32_t>(value)));
        return ok;
    }
    if (what == "objectColor") {
        const bool ok = editSelection("color", [](LotGameObject& o, int v) {
            o.color = unpackColor(v);
            o.colorByLayer = false;  // 색을 직접 골랐으면 층 따름을 끈다
        }, value);
        if (ok) LOT_LOG("color: selection -> custom");
        return ok;
    }
    if (what == "objectByLayer") {
        const bool ok = editSelection("color", [](LotGameObject& o, int v) {
            o.colorByLayer = (v != 0);
        }, value);
        if (ok) LOT_LOG("color: selection " << (value != 0 ? "-> ByLayer" : "-> own colour"));
        return ok;
    }
    if (what == "delete") {
        if (id == LotLayers::kDefault) return false;
        // 그 층의 오브젝트는 지우지 않고 기본층으로 옮긴다 (CAD 관례)
        std::set<LotGameObject::id_t> affected;
        for (auto& entry : objects) {
            if (entry.second.layer == id) affected.insert(entry.first);
        }
        EditHistory::Edit e;
        e.label = "layer delete";
        e.before = EditHistory::snapshot(objects, affected);
        for (LotGameObject::id_t objId : affected) {
            if (auto* obj = LotGameObject::find(objects, objId)) obj->layer = LotLayers::kDefault;
        }
        e.after = EditHistory::snapshot(objects, affected);
        edit.history().record(std::move(e));
        layers.remove(id);
        return true;
    }
    return false;
}

}  // namespace lot_ui
