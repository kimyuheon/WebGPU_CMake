#include "lot_feature.h"

#include "lot_brep_shape.h"
#include "lot_brep_tessellator.h"
#include "lot_log.h"
#include "lot_model.h"

#include <cmath>
#include <cstdlib>
#include <functional>

namespace lot_feature {
namespace {

using lot::LotBRepShape;
using id_t = LotGameObject::id_t;

std::vector<vec3> transformed(const mat4& m, const std::vector<vec3>& pts) {
    std::vector<vec3> out;
    out.reserve(pts.size());
    for (const vec3& p : pts) out.push_back(transformPoint(m, p));
    return out;
}

bool sameProfile(const std::vector<vec3>& a, const std::vector<vec3>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const vec3 d = a[i] - b[i];
        if (dot(d, d) > 1e-12f) return false;
    }
    return true;
}

// 솔리드 로컬로 옮긴 스케치 단면 (네이티브 worldToSolid * sketchToWorld)
bool localProfile(const LotGameObject& solid, const LotGameObject& sketch, std::vector<vec3>& out) {
    if (!sketch.isSketch() || !sketch.closed || sketch.points.size() < 3) return false;
    out = transformed(inverseOf(solid.transform), sketch.worldPoints());
    return true;
}

// 컷 / 보스 i 의 연결을 적는다 (네이티브 noteCutLink / noteBossLink). 연결 없는 솔리드(사본 등)에
// 스케치 없이 만든 것이면 적을 것이 없다.
void noteLink(LotGameObject& o, bool isCut, size_t index, id_t sketch, const std::vector<vec3>& profile,
              const mat4& worldToSolid) {
    if (!o.brep) return;
    FeatureLink L = o.featureLink ? *o.featureLink : FeatureLink{};
    if (!o.featureLink && sketch == LotGameObject::kInvalidId) return;
    const size_t n = isCut ? o.brep->feature().cuts.size() : o.brep->feature().bosses.size();
    auto& ids = isCut ? L.cutSketches : L.bossSketches;
    auto& invs = isCut ? L.cutLinkInv : L.bossLinkInv;
    auto& last = isCut ? L.lastCutProfiles : L.lastBossProfiles;
    ids.resize(n, FeatureLink::kNone);
    invs.resize(n, mat4::identity());
    last.resize(n);
    if (index < n) {
        ids[index] = sketch;
        invs[index] = worldToSolid;
        last[index] = profile;
    }
    o.featureLink = std::make_shared<const FeatureLink>(std::move(L));
}

bool isExtrudeSolid(const LotGameObject* o) {
    return o && o->brep && o->brep->feature().kind == LotBRepShape::FeatureKind::Extrude;
}

// 솔리드 하나의 형상 · 메시 · 연결을 바꾸고 한 번의 실행 취소로 남긴다
void commitSolid(LotGameObject::Map& objects, LotGameObject& o, std::shared_ptr<const LotBRepShape> shape,
                 std::shared_ptr<LotModel> model, EditHistory& history, const char* label,
                 const std::function<void(LotGameObject&)>& link) {
    EditHistory::Edit edit;
    edit.label = label;
    edit.before = {EditHistory::Record::capture(o)};
    o.brep = std::move(shape);
    o.model = std::move(model);
    if (link) link(o);
    edit.after = {EditHistory::Record::capture(o)};
    history.record(std::move(edit));
    (void)objects;
}

}  // namespace

bool sketchProfile(const LotGameObject& src, std::vector<vec3>& pts, vec3& normal, vec3& center) {
    if (!src.isSketch() || src.isHatch() || !src.closed || src.points.size() < 3) return false;
    pts = src.worldPoints();
    const mat4 m = src.transform.mat4Transform();
    if (src.curve.kind == LotGameObject::Curve::Kind::Circle) {
        const vec3 r = transformPoint(m, src.curve.right) - transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
        const vec3 u = transformPoint(m, src.curve.up) - transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
        normal = normalize(cross(r, u));
    } else {
        // 뉴얼 법선 - 첫 꼭짓점이 오목해도 방향이 맞다 (그 뒤 아래에서 결정적으로 뒤집는다)
        vec3 n{0.0f, 0.0f, 0.0f};
        for (size_t i = 0; i < pts.size(); ++i) n = n + cross(pts[i], pts[(i + 1) % pts.size()]);
        if (dot(n, n) < 1e-20f) return false;   // 일직선
        normal = normalize(n);
    }
    // 그린 방향(감김)과 무관하게: 수평이면 위, 수직이면 +Y, 그 외 +X (양수 높이가 늘 같은 쪽)
    constexpr float kFlat = 1e-4f;
    if (std::fabs(normal.z) > kFlat) {
        if (normal.z < 0.0f) normal = normal * -1.0f;
    } else if (std::fabs(normal.y) > kFlat) {
        if (normal.y < 0.0f) normal = normal * -1.0f;
    } else if (normal.x < 0.0f) {
        normal = normal * -1.0f;
    }
    vec3 sum{0.0f, 0.0f, 0.0f};
    for (const vec3& p : pts) sum = sum + p;
    center = sum * (1.0f / static_cast<float>(pts.size()));
    return true;
}

mat4 inverseOf(const TransformComponent& t) {
    mat4 r = mat4::identity();
    const vec3 cols[3] = {t.worldToLocalDirection(vec3{1.0f, 0.0f, 0.0f}),
                          t.worldToLocalDirection(vec3{0.0f, 1.0f, 0.0f}),
                          t.worldToLocalDirection(vec3{0.0f, 0.0f, 1.0f})};
    for (int c = 0; c < 3; ++c) {
        r.m[c][0] = cols[c].x;
        r.m[c][1] = cols[c].y;
        r.m[c][2] = cols[c].z;
    }
    const vec3 o = t.worldToLocalPoint(vec3{0.0f, 0.0f, 0.0f});
    r.m[3][0] = o.x;
    r.m[3][1] = o.y;
    r.m[3][2] = o.z;
    return r;
}

std::shared_ptr<LotModel> buildSolidModel(lot_web_device& device, const lot::LotBRepShape& shape) {
    lot::BRepTessellationOptions options;
    options.color = vec3{1.0f, 1.0f, 1.0f};   // 색은 객체 색 (정점 색은 흰색 - 셰이더가 객체 색을 쓴다)
    auto builder = lot::tessellateBRep(shape, options);
    if (!builder || builder->indices.size() < 3) return nullptr;
    auto model = std::make_shared<LotModel>(device, *builder);
    return model->isReady() ? model : nullptr;
}

id_t extrude(LotGameObject::Map& objects, id_t sketch, float height, lot_web_device& device, std::string& why) {
    const LotGameObject* src = LotGameObject::find(objects, sketch);
    std::vector<vec3> pts;
    vec3 n, c;
    if (!src || !sketchProfile(*src, pts, n, c)) {
        why = "closed sketches only (rectangle / circle / polygon / closed polyline)";
        return LotGameObject::kInvalidId;
    }
    if (!(std::fabs(height) >= 1e-3f)) {
        why = "height is zero";
        return LotGameObject::kInvalidId;
    }
    std::vector<vec3> local;
    local.reserve(pts.size());
    for (const vec3& p : pts) local.push_back(p - c);
    auto shape = LotBRepShape::makeExtrude(local, n, height);
    if (!shape) {
        why = "the profile crosses itself or is not flat";
        return LotGameObject::kInvalidId;
    }
    auto model = buildSolidModel(device, *shape);
    if (!model) {
        why = "tessellation failed";
        return LotGameObject::kInvalidId;
    }
    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = c;
    obj.model = std::move(model);
    obj.brep = std::move(shape);
    // 네이티브와 같이 아무 밝은 색 하나 - 여럿 만들어도 구별된다
    obj.color = vec3{0.4f + (std::rand() % 50) / 100.0f, 0.4f + (std::rand() % 50) / 100.0f,
                     0.4f + (std::rand() % 50) / 100.0f};
    obj.layer = src->layer;
    FeatureLink L;
    L.sketch = sketch;
    L.linkInv = inverseOf(obj.transform);
    L.lastProfile = local;
    obj.featureLink = std::make_shared<const FeatureLink>(std::move(L));
    const id_t id = obj.getId();
    objects.emplace(id, std::move(obj));
    return id;
}

bool cut(LotGameObject::Map& objects, id_t solidId, id_t sketchId, float depth, bool through,
         lot_web_device& device, EditHistory& history, std::string& why) {
    LotGameObject* solid = LotGameObject::find(objects, solidId);
    const LotGameObject* sketch = LotGameObject::find(objects, sketchId);
    if (!isExtrudeSolid(solid)) { why = "not an extruded solid"; return false; }
    std::vector<vec3> profile;
    if (!sketch || !localProfile(*solid, *sketch, profile)) { why = "the cut needs a closed sketch"; return false; }
    auto shape = solid->brep->cutExtrude(profile, depth, through);
    if (!shape) {
        why = "the cut is outside the solid, splits it in two, or reaches the bottom as a pocket";
        return false;
    }
    auto model = buildSolidModel(device, *shape);
    if (!model) { why = "tessellation failed"; return false; }
    const mat4 worldToSolid = inverseOf(solid->transform);
    commitSolid(objects, *solid, shape, std::move(model), history, through ? "cut through" : "pocket cut",
                [&](LotGameObject& o) { noteLink(o, true, o.brep->feature().cuts.size() - 1, sketchId, profile, worldToSolid); });
    LOT_LOG("feature: " << (through ? "through cut" : "pocket cut") << " on solid " << solidId << " from sketch "
            << sketchId << (shape->layered() ? " (layered)" : "") << ", volume " << shape->volume());
    return true;
}

bool boss(LotGameObject::Map& objects, id_t solidId, id_t sketchId, float height, lot_web_device& device,
          EditHistory& history, std::string& why) {
    LotGameObject* solid = LotGameObject::find(objects, solidId);
    const LotGameObject* sketch = LotGameObject::find(objects, sketchId);
    if (!isExtrudeSolid(solid)) { why = "not an extruded solid"; return false; }
    if (!(height > 0.0f)) { why = "the boss height must be positive"; return false; }
    std::vector<vec3> profile;
    if (!sketch || !localProfile(*solid, *sketch, profile)) { why = "the boss needs a closed sketch"; return false; }
    // 스케치가 어느 캡에 가까운가 - 위 캡이면 위로, 아래 캡이면 아래로 쌓는다 (네이티브와 같다)
    const auto& F = solid->brep->feature();
    if (F.profile.empty()) { why = "empty solid"; return false; }
    const float level = dot(profile.front() - F.profile.front(), F.direction);
    const bool onTop = std::fabs(level - F.height) <= std::fabs(level);
    auto shape = solid->brep->addBoss(profile, onTop ? height : -height);
    if (!shape) { why = "the boss floats off the solid or crosses itself"; return false; }
    auto model = buildSolidModel(device, *shape);
    if (!model) { why = "tessellation failed"; return false; }
    const mat4 worldToSolid = inverseOf(solid->transform);
    commitSolid(objects, *solid, shape, std::move(model), history, "boss",
                [&](LotGameObject& o) { noteLink(o, false, o.brep->feature().bosses.size() - 1, sketchId, profile, worldToSolid); });
    LOT_LOG("feature: boss on solid " << solidId << " from sketch " << sketchId << (onTop ? " (top)" : " (bottom)")
            << (shape->layered() ? " (layered)" : "") << ", volume " << shape->volume());
    return true;
}

bool setHeight(LotGameObject::Map& objects, id_t solidId, float height, lot_web_device& device,
               EditHistory& history, std::string& why) {
    LotGameObject* solid = LotGameObject::find(objects, solidId);
    if (!isExtrudeSolid(solid)) { why = "not an extruded solid"; return false; }
    if (!(height > 1e-3f)) { why = "the height must be positive"; return false; }
    const auto& F = solid->brep->feature();
    auto shape = LotBRepShape::makeCutExtrude(F.profile, F.direction, height, solid->brep->remakeCuts(), F.bosses);
    if (!shape) { why = "a pocket would reach the bottom at this height"; return false; }
    auto model = buildSolidModel(device, *shape);
    if (!model) { why = "tessellation failed"; return false; }
    commitSolid(objects, *solid, shape, std::move(model), history, "extrude height", nullptr);
    LOT_LOG("feature: solid " << solidId << " height " << height);
    return true;
}

int regenerate(LotGameObject::Map& objects, lot_web_device& device) {
    int count = 0;
    for (auto& entry : objects) {
        LotGameObject& o = entry.second;
        if (!o.featureLink || !isExtrudeSolid(&o)) continue;
        FeatureLink L = *o.featureLink;
        const auto& F = o.brep->feature();
        std::string err;
        auto fromSketch = [&](unsigned sid, const mat4& inv, std::vector<vec3>& out) {
            const LotGameObject* s = LotGameObject::find(objects, sid);
            if (!s) { err = "sketch #" + std::to_string(sid) + " is gone"; return false; }
            if (!s->isSketch() || !s->closed || s->points.size() < 3) {
                err = "sketch #" + std::to_string(sid) + " is not a closed shape";
                return false;
            }
            out = transformed(inv, s->worldPoints());
            return true;
        };
        bool changed = false, ok = true;
        std::vector<vec3> profile = F.profile;
        if (L.sketch != FeatureLink::kNone) {
            if (!fromSketch(L.sketch, L.linkInv, profile)) ok = false;
            else if (!sameProfile(profile, L.lastProfile)) changed = true;
        }
        auto cuts = o.brep->remakeCuts();
        L.cutSketches.resize(cuts.size(), FeatureLink::kNone);
        L.cutLinkInv.resize(cuts.size(), mat4::identity());
        L.lastCutProfiles.resize(cuts.size());
        std::vector<std::vector<vec3>> cutInputs(cuts.size());
        for (size_t i = 0; ok && i < cuts.size(); ++i) {
            if (L.cutSketches[i] == FeatureLink::kNone) continue;
            if (!fromSketch(L.cutSketches[i], L.cutLinkInv[i], cutInputs[i])) { ok = false; break; }
            if (!sameProfile(cutInputs[i], L.lastCutProfiles[i])) changed = true;
            cuts[i].profile = cutInputs[i];
        }
        auto bosses = F.bosses;
        L.bossSketches.resize(bosses.size(), FeatureLink::kNone);
        L.bossLinkInv.resize(bosses.size(), mat4::identity());
        L.lastBossProfiles.resize(bosses.size());
        std::vector<std::vector<vec3>> bossInputs(bosses.size());
        for (size_t i = 0; ok && i < bosses.size(); ++i) {
            if (L.bossSketches[i] == FeatureLink::kNone) continue;
            if (!fromSketch(L.bossSketches[i], L.bossLinkInv[i], bossInputs[i])) { ok = false; break; }
            if (!sameProfile(bossInputs[i], L.lastBossProfiles[i])) changed = true;
            bosses[i].profile = bossInputs[i];
        }
        auto setError = [&](const std::string& e) {
            if (o.featureLink->error == e) return;
            FeatureLink E = *o.featureLink;
            E.error = e;
            o.featureLink = std::make_shared<const FeatureLink>(std::move(E));
            if (!e.empty()) LOT_LOG("feature: solid " << entry.first << " - " << e);
        };
        if (!ok) { setError(err); continue; }
        if (!changed) { setError(""); continue; }
        auto shape = LotBRepShape::makeCutExtrude(profile, F.direction, F.height, cuts, bosses);
        if (!shape) {
            setError("cannot rebuild - the profile left its plane, or a cut/boss overlaps or leaves the solid");
            continue;
        }
        auto model = buildSolidModel(device, *shape);
        if (!model) { setError("cannot rebuild - tessellation failed"); continue; }
        o.model = std::move(model);
        o.brep = std::move(shape);
        if (L.sketch != FeatureLink::kNone) L.lastProfile = profile;
        for (size_t i = 0; i < cuts.size(); ++i) if (L.cutSketches[i] != FeatureLink::kNone) L.lastCutProfiles[i] = cutInputs[i];
        for (size_t i = 0; i < bosses.size(); ++i) if (L.bossSketches[i] != FeatureLink::kNone) L.lastBossProfiles[i] = bossInputs[i];
        L.error.clear();
        o.featureLink = std::make_shared<const FeatureLink>(std::move(L));
        ++count;
        LOT_LOG("feature: solid " << entry.first << " regenerated from its sketches");
    }
    return count;
}

}  // namespace lot_feature
