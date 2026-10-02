#include "lot_history.h"
#include "lot_log.h"

#include <cmath>

namespace {

bool same(const vec3& a, const vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool same(const quat& a, const quat& b) {
    return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z;
}

bool same(const EditHistory::Record& a, const EditHistory::Record& b) {
    return a.id == b.id
        && same(a.transform.translation, b.transform.translation)
        && same(a.transform.scale, b.transform.scale)
        && same(a.transform.rotation, b.transform.rotation)
        && same(a.color, b.color)
        && a.layer == b.layer && a.linetype == b.linetype && a.colorByLayer == b.colorByLayer
        && a.model == b.model
        && a.material == b.material
        && a.closed == b.closed
        && a.text.valid == b.text.valid && a.text.content == b.text.content
        && a.text.height == b.text.height
        && a.dim.valid == b.dim.valid
        && same(a.dim.p1, b.dim.p1) && same(a.dim.p2, b.dim.p2) && same(a.dim.dimLine, b.dim.dimLine)
        && a.points.size() == b.points.size()
        && [&] {
               for (size_t i = 0; i < a.points.size(); ++i) {
                   if (!same(a.points[i], b.points[i])) return false;
               }
               return true;
           }();
}

bool same(const std::vector<EditHistory::Record>& a, const std::vector<EditHistory::Record>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!same(a[i], b[i])) return false;
    }
    return true;
}

}  // namespace

EditHistory::Record EditHistory::Record::capture(const LotGameObject& obj) {
    Record r;
    r.id = obj.getId();
    r.transform = obj.transform;
    r.color = obj.color;
    r.layer = obj.layer;
    r.linetype = obj.linetype;
    r.colorByLayer = obj.colorByLayer;
    r.model = obj.model;
    r.material = obj.material;
    r.points = obj.points;
    r.closed = obj.closed;
    r.curve = obj.curve;
    r.dim = obj.dim;
    r.text = obj.text;
    r.light = obj.light;
    return r;
}

void EditHistory::Record::apply(LotGameObject& obj) const {
    obj.transform = transform;
    obj.color = color;
    obj.layer = layer;
    obj.linetype = linetype;
    obj.colorByLayer = colorByLayer;
    obj.model = model;
    obj.material = material;
    obj.points = points;
    obj.closed = closed;
    obj.curve = curve;
    obj.dim = dim;
    obj.text = text;
    obj.light = light;
}

EditHistory::Record EditHistory::snapshot(const LotGameObject::Map& objects, id_t id) {
    const LotGameObject* obj = LotGameObject::find(objects, id);
    return obj ? Record::capture(*obj) : Record{};
}

std::vector<EditHistory::Record> EditHistory::snapshot(const LotGameObject::Map& objects,
                                                       const std::set<id_t>& ids) {
    std::vector<Record> out;
    out.reserve(ids.size());
    for (id_t id : ids) {
        if (const auto* obj = LotGameObject::find(objects, id)) out.push_back(Record::capture(*obj));
    }
    return out;
}

void EditHistory::recordCreated(const char* label, const LotGameObject::Map& objects,
                                const std::set<id_t>& ids) {
    Edit e;
    e.label = label;
    e.after = snapshot(objects, ids);
    record(std::move(e));
}

void EditHistory::recordCreated(const char* label, const LotGameObject::Map& objects, id_t id) {
    recordCreated(label, objects, std::set<id_t>{id});
}

void EditHistory::record(Edit edit) {
    if (same(edit.before, edit.after)) return;  // 아무것도 안 바뀐 편집은 기록하지 않는다
    redo_.clear();
    undo_.push_back(std::move(edit));
    if (undo_.size() > maxEntries) undo_.erase(undo_.begin());
    LOT_LOG("history: " << undo_.back().label << " recorded (" << undo_.size() << " undoable)");
}

std::set<EditHistory::id_t> EditHistory::apply(LotGameObject::Map& objects,
                                               const std::vector<Record>& from,
                                               const std::vector<Record>& to) {
    std::set<id_t> touched;

    // 1. 'from' 에만 있는 것은 지운다 (되돌리기라면 편집이 만든 것, 다시 실행이라면 편집이 지운 것)
    for (const Record& r : from) {
        bool inTo = false;
        for (const Record& t : to) {
            if (t.id == r.id) { inTo = true; break; }
        }
        if (!inTo) {
            objects.erase(r.id);
            touched.insert(r.id);
        }
    }

    // 2. 'to' 의 상태를 적용한다. 없으면 같은 id 로 다시 만든다.
    for (const Record& r : to) {
        LotGameObject* obj = LotGameObject::find(objects, r.id);
        if (!obj) {
            auto created = LotGameObject::createWithId(r.id);
            obj = &objects.emplace(r.id, std::move(created)).first->second;
        }
        r.apply(*obj);
        touched.insert(r.id);
    }
    return touched;
}

std::set<EditHistory::id_t> EditHistory::undo(LotGameObject::Map& objects) {
    if (undo_.empty()) {
        LOT_LOG("history: nothing to undo");
        return {};
    }
    Edit e = std::move(undo_.back());
    undo_.pop_back();
    std::set<id_t> touched = apply(objects, e.after, e.before);
    LOT_LOG("history: undo " << e.label << " (" << undo_.size() << " left)");
    redo_.push_back(std::move(e));
    return touched;
}

std::set<EditHistory::id_t> EditHistory::redo(LotGameObject::Map& objects) {
    if (redo_.empty()) {
        LOT_LOG("history: nothing to redo");
        return {};
    }
    Edit e = std::move(redo_.back());
    redo_.pop_back();
    std::set<id_t> touched = apply(objects, e.before, e.after);
    LOT_LOG("history: redo " << e.label << " (" << redo_.size() << " left)");
    undo_.push_back(std::move(e));
    return touched;
}
