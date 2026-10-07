#include "lot_edit_ops.h"
#include "lot_log.h"

#include <cmath>
#include <deque>
#include <vector>

namespace lot_edit_ops {
namespace {

float len3(const vec3& v) { return std::sqrt(dot(v, v)); }

// 원본의 층 · 색 · 선종류를 물려받은 새 스케치 (월드 점, 무게중심이 원점)
LotGameObject makeSketch(const LotGameObject& src, const std::vector<vec3>& world, bool closed) {
    LotGameObject obj = LotGameObject::createGameObject();
    vec3 origin{0.0f, 0.0f, 0.0f};
    for (const vec3& p : world) origin = origin + p;
    origin = origin * (1.0f / static_cast<float>(world.size()));
    obj.transform.translation = origin;
    obj.color = src.color;
    obj.colorByLayer = src.colorByLayer;
    obj.layer = src.layer;
    obj.linetype = src.linetype;
    obj.closed = closed;
    for (const vec3& p : world) obj.points.push_back(p - origin);
    return obj;
}

// 선 · 폴리선만 (곡선 정의가 있는 원/호는 아니다)
bool isPath(const LotGameObject& o) { return o.isSketch() && !o.hasCurve() && o.points.size() >= 2; }

}  // namespace

std::set<LotGameObject::id_t> explode(LotGameObject::Map& objects, const std::set<LotGameObject::id_t>& selection,
                                      EditHistory& history) {
    std::set<LotGameObject::id_t> sources, made;
    for (LotGameObject::id_t id : selection) {
        const LotGameObject* o = LotGameObject::find(objects, id);
        if (o && isPath(*o) && (o->points.size() > 2 || o->closed)) sources.insert(id);
    }
    if (sources.empty()) {
        LOT_LOG("explode: nothing to explode (polylines, rectangles and polygons only)");
        return made;
    }
    EditHistory::Edit edit;
    edit.label = "explode";
    edit.before = EditHistory::snapshot(objects, sources);
    for (LotGameObject::id_t id : sources) {
        const LotGameObject& o = *LotGameObject::find(objects, id);
        const std::vector<vec3> pts = o.worldPoints();
        const size_t n = pts.size();
        const size_t segs = o.closed ? n : n - 1;
        for (size_t i = 0; i < segs; ++i) {
            const vec3& a = pts[i];
            const vec3& b = pts[(i + 1) % n];
            if (len3(b - a) < 1e-6f) continue;   // 길이 0 인 변 (닫힌 폴리선의 겹친 끝점 등)
            LotGameObject line = makeSketch(o, {a, b}, false);
            const auto nid = line.getId();
            made.insert(nid);
            objects.emplace(nid, std::move(line));
        }
    }
    for (LotGameObject::id_t id : sources) objects.erase(id);
    edit.after = EditHistory::snapshot(objects, made);
    history.record(std::move(edit));
    LOT_LOG("explode: " << sources.size() << " objects -> " << made.size() << " lines");
    return made;
}

LotGameObject::id_t join(LotGameObject::Map& objects, const std::set<LotGameObject::id_t>& selection,
                         EditHistory& history, std::string& why) {
    constexpr float kTol = 1e-3f;   // 네이티브 join_tool 과 같다
    struct Piece {
        LotGameObject::id_t id;
        std::vector<vec3> pts;
    };
    std::vector<Piece> pieces;
    for (LotGameObject::id_t id : selection) {
        const LotGameObject* o = LotGameObject::find(objects, id);
        if (!o || !isPath(*o) || o->closed) continue;   // 닫힌 것은 이을 끝이 없다
        pieces.push_back(Piece{id, o->worldPoints()});
    }
    if (pieces.size() < 2) {
        why = "select two or more lines / open polylines";
        return LotGameObject::kInvalidId;
    }
    // 첫 조각에서 시작해 양 끝에 맞는 조각을 (필요하면 뒤집어) 붙여 나간다
    std::deque<vec3> chain(pieces[0].pts.begin(), pieces[0].pts.end());
    std::vector<bool> used(pieces.size(), false);
    used[0] = true;
    size_t left = pieces.size() - 1;
    while (left > 0) {
        bool grew = false;
        for (size_t i = 0; i < pieces.size() && !grew; ++i) {
            if (used[i]) continue;
            const std::vector<vec3>& p = pieces[i].pts;
            if (len3(p.front() - chain.back()) <= kTol) {
                chain.insert(chain.end(), p.begin() + 1, p.end());
            } else if (len3(p.back() - chain.back()) <= kTol) {
                chain.insert(chain.end(), p.rbegin() + 1, p.rend());
            } else if (len3(p.back() - chain.front()) <= kTol) {
                chain.insert(chain.begin(), p.begin(), p.end() - 1);
            } else if (len3(p.front() - chain.front()) <= kTol) {
                chain.insert(chain.begin(), p.rbegin(), p.rend() - 1);
            } else {
                continue;
            }
            used[i] = true;
            --left;
            grew = true;
        }
        if (!grew) {
            why = std::to_string(left) + " piece(s) do not touch the chain (ends must meet within 0.001)";
            return LotGameObject::kInvalidId;
        }
    }
    std::vector<vec3> pts(chain.begin(), chain.end());
    bool closed = false;
    if (pts.size() >= 3 && len3(pts.front() - pts.back()) <= kTol) {
        closed = true;
        pts.pop_back();   // 닫히면 겹친 끝점은 버린다
    }
    std::set<LotGameObject::id_t> sources;
    for (const Piece& p : pieces) sources.insert(p.id);
    EditHistory::Edit edit;
    edit.label = "join";
    edit.before = EditHistory::snapshot(objects, sources);
    LotGameObject joined = makeSketch(*LotGameObject::find(objects, pieces[0].id), pts, closed);
    const auto nid = joined.getId();
    objects.emplace(nid, std::move(joined));
    for (LotGameObject::id_t id : sources) objects.erase(id);
    edit.after = EditHistory::snapshot(objects, std::set<LotGameObject::id_t>{nid});
    history.record(std::move(edit));
    LOT_LOG("join: " << pieces.size() << " pieces -> polyline " << nid << " (" << pts.size() << " points"
            << (closed ? ", closed" : "") << ")");
    return nid;
}

}  // namespace lot_edit_ops
