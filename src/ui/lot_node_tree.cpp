#include "lot_node_tree.h"
#include "lot_log.h"
#include "lot_ui_command.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_ui {

std::string LotNodeTree::label(const LotGameObject& obj, LotGameObject::id_t id) {
    const std::string tag = " #" + std::to_string(id);
    if (obj.isText()) {
        // 내용 앞부분만 (UTF-8 글자 중간에서 자르지 않게 바이트 단위로 경계를 찾는다)
        std::string s = obj.text.content;
        if (s.size() > 24) {
            size_t cut = 24;
            while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
            s = s.substr(0, cut) + "…";
        }
        return "문자 '" + s + "'" + tag;
    }
    if (obj.isDimension()) return "치수" + tag;
    if (obj.isLight()) return "광원" + tag;
    if (obj.isSolid()) return std::string(obj.featureLink && !obj.featureLink->error.empty() ? "솔리드 (오류)" : "솔리드") + tag;
    if (obj.isHatch()) return "해치 " + obj.hatch->patternName + tag;
    if (obj.isSketch()) {
        if (obj.curve.kind == LotGameObject::Curve::Kind::Circle) return "원" + tag;
        if (obj.curve.kind == LotGameObject::Curve::Kind::Arc) return "호" + tag;
        if (obj.points.size() == 2) return "선" + tag;
        return std::string(obj.closed ? "닫힌 폴리선" : "폴리선") + " (" + std::to_string(obj.points.size()) + "점)" + tag;
    }
    if (obj.model) return "메시" + tag;
    return "객체" + tag;
}

void LotNodeTree::refresh(const LotGameObject::Map& objects, const EditController& edit) {
    const uint64_t rev = edit.history().revision();
    if (objects_ == &objects && revision_ == rev && count_ == objects.size()
        && builtHiddenVersion_ == hiddenVersion_) {
        return;
    }
    objects_ = &objects;
    revision_ = rev;
    count_ = objects.size();
    builtHiddenVersion_ = hiddenVersion_;
    counts_.clear();
    hiddenCounts_.clear();
    ids_.clear();
    for (const auto& entry : objects) {
        const uint32_t layer = entry.second.layer;
        ++counts_[layer];
        if (entry.second.hidden) ++hiddenCounts_[layer];
        ids_[layer].push_back(entry.first);
    }
    for (auto& kv : ids_) std::sort(kv.second.begin(), kv.second.end());
}

const std::unordered_map<uint32_t, int>& LotNodeTree::layerCounts(const LotGameObject::Map& objects,
                                                                  const EditController& edit) {
    refresh(objects, edit);
    return counts_;
}

std::string LotNodeTree::summaryJson(const LotLayers& layers, const LotGameObject::Map& objects,
                                     const EditController& edit) {
    refresh(objects, edit);

    int total = 0, hidden = 0;
    std::string j = "{\"layers\":[";
    bool first = true;
    for (const LotLayers::Layer* l : layers.all()) {
        const auto c = counts_.find(l->id);
        const auto h = hiddenCounts_.find(l->id);
        const int n = (c == counts_.end()) ? 0 : c->second;
        const int nh = (h == hiddenCounts_.end()) ? 0 : h->second;
        total += n;
        hidden += nh;
        if (!first) j += ",";
        first = false;
        j += "{\"id\":" + std::to_string(l->id) + ",\"name\":" + quote(l->name)
           + ",\"count\":" + std::to_string(n) + ",\"hidden\":" + std::to_string(nh)
           + ",\"visible\":" + (l->visible ? "true" : "false") + "}";
    }
    // 선택은 많으면 앞의 것만 (트리 강조용 - 수천 개를 다 칠할 일은 없다)
    j += "],\"total\":" + std::to_string(total) + ",\"hiddenTotal\":" + std::to_string(hidden)
       + ",\"selectedCount\":" + std::to_string(edit.selection().size()) + ",\"selected\":[";
    int k = 0;
    for (LotGameObject::id_t id : edit.selection()) {
        if (k >= 2000) break;
        if (k++) j += ",";
        j += std::to_string(id);
    }
    j += "]}";

    if (j == last_) return std::string();
    last_ = j;
    return j;
}

std::string LotNodeTree::childrenJson(uint32_t layerId, int offset, int limit,
                                      const LotGameObject::Map& objects, const EditController& edit) {
    refresh(objects, edit);
    const auto it = ids_.find(layerId);
    const int total = (it == ids_.end()) ? 0 : static_cast<int>(it->second.size());
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = 200;
    std::string j = "{\"layer\":" + std::to_string(layerId) + ",\"offset\":" + std::to_string(offset)
                  + ",\"total\":" + std::to_string(total) + ",\"items\":[";
    for (int i = offset; i < total && i < offset + limit; ++i) {
        const LotGameObject::id_t id = it->second[static_cast<size_t>(i)];
        const LotGameObject* obj = LotGameObject::find(objects, id);
        if (!obj) continue;
        if (i > offset) j += ",";
        j += "{\"id\":" + std::to_string(id) + ",\"label\":" + quote(label(*obj, id))
           + ",\"hidden\":" + (obj->hidden ? "true" : "false")
           + ",\"selected\":" + (edit.selection().count(id) ? "true" : "false") + "}";
    }
    j += "]}";
    return j;
}

void LotNodeTree::setHidden(LotGameObject::id_t id, bool hide, LotGameObject::Map& objects,
                            EditController& edit) {
    int changed = 0;
    auto apply = [&](LotGameObject::id_t oid, LotGameObject& obj) {
        if (obj.hidden == hide) return;
        obj.hidden = hide;
        ++changed;
        (void)oid;
    };
    if (id == LotGameObject::kInvalidId) {
        for (auto& entry : objects) apply(entry.first, entry.second);
    } else if (LotGameObject* obj = LotGameObject::find(objects, id)) {
        apply(id, *obj);
    }
    if (hide && changed) {
        std::set<LotGameObject::id_t> keep;
        for (LotGameObject::id_t sel : edit.selection()) {
            const LotGameObject* o = LotGameObject::find(objects, sel);
            if (o && !o->hidden) keep.insert(sel);
        }
        edit.setSelection(std::move(keep));
    }
    ++hiddenVersion_;
    LOT_LOG("tree: " << (hide ? "hid " : "showed ") << changed << " objects");
}

}  // namespace lot_ui
