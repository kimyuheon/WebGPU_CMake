// DXF 펼치기: 엔티티 -> 객체 (층 · 색 상속, OCS, 블록 · 치수 · 표 전개, 공용 추가 함수).
#include "dxf/lot_dxf_emit.h"

#include "lot_linetype.h"
#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_dxf {
namespace detail {

void Emitter::noteSkipped(const std::string& type) {
    ++stats.skipped;
    if (stats.skippedKinds.find(type) == std::string::npos) {
        if (!stats.skippedKinds.empty()) stats.skippedKinds += ", ";
        stats.skippedKinds += type;
    }
}

uint32_t Emitter::layerOf(const Entity& e) {
    const std::string name = e.str(8, "0");
    auto it = layerIds.find(name);
    if (it != layerIds.end()) return it->second;
    const uint32_t id = layers.create(name, vec3{0.8f, 0.8f, 0.85f});
    layerIds.emplace(name, id);
    return id;
}

// 엔티티 색: 62 가 있으면 그 색, 없거나 256 이면 층 따름.
vec3 Emitter::colorOf(const Entity& e, uint32_t layerId, bool& byLayer) {
    const int aci = e.integer(62, 256);
    if (aci == 256 || aci <= 0) {
        byLayer = true;
        const LotLayers::Layer* l = layers.find(layerId);
        return l ? l->color : vec3{0.9f, 0.9f, 0.9f};
    }
    byLayer = false;
    return aciColor(aci);
}

void Emitter::addSketch(std::vector<vec3> pts, bool closed, const Entity& e, const LotGameObject::Curve* curve) {
    if (pts.size() < 2) return;
    const uint32_t layerId = layerOf(e);
    bool byLayer = false;
    const vec3 color = colorOf(e, layerId, byLayer);

    vec3 origin{0.0f, 0.0f, 0.0f};
    if (curve) {
        origin = curve->center;
    } else {
        for (const vec3& p : pts) origin = origin + p;
        origin = origin * (1.0f / static_cast<float>(pts.size()));
    }
    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = origin;
    obj.color = color;
    obj.colorByLayer = byLayer;
    obj.layer = layerId;
    obj.linetype = e.has(6) ? linetypeByName(e.str(6)) : lot_linetype::kByLayer;
    obj.closed = closed;
    obj.points.reserve(pts.size());
    for (const vec3& p : pts) obj.points.push_back(p - origin);
    if (curve) {
        obj.curve = *curve;
        obj.curve.center = vec3{0.0f, 0.0f, 0.0f};
    }
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
}

// 글자 한 줄. at 은 월드 기준점, right 는 진행 방향 (단위), height 는 월드 높이.
void Emitter::addText(const std::string& content, const Entity& e, const vec3& at, const vec3& right, float height,
                      int hAlign, int vAlign) {
    if (content.empty() || !(height > 0.0f)) return;
    const uint32_t layerId = layerOf(e);
    bool byLayer = false;
    const vec3 color = colorOf(e, layerId, byLayer);
    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = at;
    obj.color = color;
    obj.colorByLayer = byLayer;
    obj.layer = layerId;
    obj.text.valid = true;
    obj.text.content = content;
    obj.text.height = height;
    obj.text.right = right;
    // 거울 삽입이어도 글자는 읽히게 (AutoCAD 의 MIRRTEXT 0) - 위는 늘 진행 방향의 왼쪽
    obj.text.up = vec3{-right.y, right.x, 0.0f};
    obj.text.hAlign = hAlign;
    obj.text.vAlign = vAlign;
    objects.emplace(obj.getId(), std::move(obj));
    ++stats.texts;
}

// 해치 객체: 바깥 경계(넓이가 가장 큰 루프)를 점으로 (피킹 · 범위), 무늬 선분은 지금 만들어 둔다.
// o / right / up 은 해치 평면 (월드). 평면 좌표는 객체 원점 기준 로컬로 옮긴다.
void Emitter::addHatch(std::shared_ptr<lot_hatch::HatchData> h, const vec3& o, const vec3& right, const vec3& up,
                       const Entity& e) {
    const uint32_t layerId = layerOf(e);
    bool byLayer = false;
    const vec3 color = colorOf(e, layerId, byLayer);
    h->origin = vec3{0.0f, 0.0f, 0.0f};
    h->right = right;
    h->up = up;
    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = o;
    obj.color = color;
    obj.colorByLayer = byLayer;
    obj.layer = layerId;
    obj.linetype = lot_linetype::kByLayer;
    lot_hatch::attach(obj, std::move(h));
    objects.emplace(obj.getId(), std::move(obj));
    ++stats.hatches;
}

void Emitter::addMesh(const std::vector<vec3>& tris, const Entity& e) {
    if (tris.size() < 3) return;
    const uint32_t layerId = layerOf(e);
    bool byLayer = false;
    const vec3 color = colorOf(e, layerId, byLayer);
    stats.faces += static_cast<int>(tris.size() / 3);
    if (!stats.meshTriangles.empty() && lastMeshObjects == objects.size() && lastMeshKey.layer == layerId
        && lastMeshKey.byLayer == byLayer && lastMeshKey.color.x == color.x && lastMeshKey.color.y == color.y
        && lastMeshKey.color.z == color.z) {
        auto& last = stats.meshTriangles.back();
        const vec3 o = objects.at(last.first).transform.translation;
        for (const vec3& p : tris) last.second.push_back(p - o);
        return;
    }
    vec3 mn = tris[0], mx = tris[0];
    for (const vec3& p : tris) {
        mn = vec3{std::fmin(mn.x, p.x), std::fmin(mn.y, p.y), std::fmin(mn.z, p.z)};
        mx = vec3{std::fmax(mx.x, p.x), std::fmax(mx.y, p.y), std::fmax(mx.z, p.z)};
    }
    auto obj = LotGameObject::createGameObject();
    obj.transform.translation = (mn + mx) * 0.5f;
    obj.color = color;
    obj.colorByLayer = byLayer;
    obj.layer = layerId;
    std::vector<vec3> local;
    local.reserve(tris.size());
    for (const vec3& p : tris) local.push_back(p - obj.transform.translation);
    stats.meshTriangles.emplace_back(obj.getId(), std::move(local));
    objects.emplace(obj.getId(), std::move(obj));
    ++stats.meshes;
    lastMeshObjects = objects.size();
    lastMeshKey = MeshKey{layerId, byLayer, color};
}

// 다각형 면 (정점 번호 목록) -> 삼각형 팬. 번호가 범위 밖이면 그 면은 버린다 (네이티브 appendFace).
void Emitter::appendFace(std::vector<vec3>& tris, const std::vector<vec3>& v, const std::vector<int>& idx,
                         const Xform& x) {
    if (idx.size() < 3) return;
    for (int i : idx) if (i < 0 || i >= static_cast<int>(v.size())) return;
    for (size_t k = 1; k + 1 < idx.size(); ++k) {
        tris.push_back(x.point(v[idx[0]]));
        tris.push_back(x.point(v[idx[k]]));
        tris.push_back(x.point(v[idx[k + 1]]));
    }
}

void Emitter::expandBlock(const std::string& name, const Xform& placed, const Entity& owner, int depth) {
    auto it = blocks.find(name);
    if (it == blocks.end() || depth > 16) return;
    Xform toBase;
    toBase.t = it->second.base * -1.0f;
    const Xform x = placed.then(toBase);
    for (const Entity& child : it->second.entities) emit(child, x, &owner, depth + 1);
}

// 엔티티 하나 (parent 는 이 엔티티를 놓은 INSERT, 맨 바깥이면 nullptr). 블록 안에서 층 "0" 인 것은
// 삽입한 층을, 색 BYBLOCK(0) 은 삽입의 색을 따른다 (AutoCAD 규칙). 종류별 처리는 emit* (lot_dxf_emit_*.cpp).
void Emitter::emit(const Entity& src, const Xform& x, const Entity* parent, int depth) {
    // 블록 안 엔티티의 층/색을 삽입한 쪽에서 물려받는다
    Entity inherited;
    const Entity* ep = &src;
    if (parent && (src.str(8, "0") == "0" || src.integer(62, 256) == 0)) {
        inherited = src;
        if (src.str(8, "0") == "0") { inherited.values.erase(8); inherited.values.emplace(8, parent->str(8, "0")); }
        if (src.integer(62, 256) == 0) {
            inherited.values.erase(62);
            if (parent->has(62)) inherited.values.emplace(62, parent->str(62));
        }
        ep = &inherited;
    }
    const Entity& e = *ep;
    const std::string& t = e.type;

    // OCS: 2D 엔티티(원·호·폴리선·문자·삽입)는 돌출 방향(210/220/230) 기준 좌표다.
    // 실제 도면에서 만나는 것은 거의 (0,0,-1) - 거울 복사한 것 - 이고, 그때 월드는
    // (-x, y, -z) 다 (임의 축 알고리즘). LINE/MTEXT/SPLINE 은 월드 좌표라 해당 없다.
    if (e.num(230, 1.0f) < 0.0f && t != "LINE" && t != "MTEXT" && t != "SPLINE" && t != "DIMENSION"
        && t != "ELLIPSE" && t != "LEADER" && t != "ACAD_TABLE"
        && t != "3DFACE" && t != "MESH" && t != "PMESH"   // 월드 좌표 (OCS 아님)
        && std::fabs(e.num(210)) < 1.0f / 64.0f && std::fabs(e.num(220)) < 1.0f / 64.0f) {
        Entity flat = e;
        flat.values.erase(230);
        Xform mirror;
        mirror.a = -1.0f;
        emit(flat, x.then(mirror), parent, depth);
        return;
    }


    if (t == "LINE") emitLine(e, x, parent, depth);
    else if (t == "CIRCLE" || t == "ARC") emitCircle(e, x, parent, depth);
    else if (t == "LWPOLYLINE") emitLwPolyline(e, x, parent, depth);
    else if (t == "TEXT" || t == "ATTRIB" || t == "ATTDEF") emitText(e, x, parent, depth);
    else if (t == "MTEXT") emitMText(e, x, parent, depth);
    else if (t == "SPLINE") emitSpline(e, x, parent, depth);
    else if (t == "HATCH") emitHatch(e, x, parent, depth);
    else if (t == "ELLIPSE") emitEllipse(e, x, parent, depth);
    else if (t == "LEADER") emitLeader(e, x, parent, depth);
    else if (t == "ACAD_TABLE") emitTable(e, x, parent, depth);
    else if (t == "3DFACE") emit3dFace(e, x, parent, depth);
    else if (t == "MESH") emitMesh(e, x, parent, depth);
    else if (t == "PMESH") emitPolyMesh(e, x, parent, depth);
    else if (t == "SOLID" || t == "TRACE") emitSolid(e, x, parent, depth);
    else if (t == "INSERT") emitInsert(e, x, parent, depth);
    else if (t == "DIMENSION") emitDimension(e, x, parent, depth);
    else if (t == "VERTEX" || t == "POLYLINE" || t == "VPORT" || t == "LTYPE" || t == "STYLE" || t == "APPID" || t == "DIMSTYLE" || t == "UCS" || t == "VIEW" || t == "CLASS" || t == "DICTIONARY" || t == "XRECORD" || t == "VISUALSTYLE" || t == "VIEWPORT" || t == "SCALE" || t == "DICTIONARYVAR" || t == "LAYOUT" || t == "TABLE" || t == "ENDTAB" || t == "MLINESTYLE" || t == "PLOTSETTINGS" || t == "TABLESTYLE") {}   // 구조용 항목 - 세지 않는다
    else if (!t.empty()) noteSkipped(t);
}

// 표: 딸린 익명 블록(코드 2)을 삽입점만큼 옮겨 펼친다 (회전 · 축척은 없다 - 네이티브와 같다)
void Emitter::emitTable(const Entity& e, const Xform& x, const Entity* /*parent*/, int depth) {
    const std::string& t = e.type;
    Xform local;
    local.t = vec3{e.num(10), e.num(20), e.num(30)};
    if (blocks.count(e.str(2))) { expandBlock(e.str(2), x.then(local), e, depth); ++stats.tables; }
    else noteSkipped("ACAD_TABLE");
}

// 블록 놓기: 기준점 10, 축척 41/42, 회전 50, 배열 70 열 × 71 행 (간격 44/45)
void Emitter::emitInsert(const Entity& e, const Xform& x, const Entity* /*parent*/, int depth) {
    const std::string& t = e.type;
    const float sx = e.num(41, 1.0f), sy = e.num(42, 1.0f);
    const float rot = e.num(50) * kDegToRad;
    const float cs = std::cos(rot), sn = std::sin(rot);
    const int cols = std::max(1, e.integer(70, 1)), rows = std::max(1, e.integer(71, 1));
    const float dc = e.num(44), dr = e.num(45);
    for (int r = 0; r < rows && r < 100; ++r) {
        for (int cI = 0; cI < cols && cI < 100; ++cI) {
            Xform local;
            local.a = cs * sx;  local.b = -sn * sy;
            local.c = sn * sx;  local.d = cs * sy;
            // 배열 간격은 블록 회전 축을 따라
            const float ox = cI * dc, oy = r * dr;
            local.t = vec3{e.num(10) + cs * ox - sn * oy, e.num(20) + sn * ox + cs * oy, e.num(30)};
            expandBlock(e.str(2), x.then(local), e, depth);
        }
    }
    ++inserts;
}

// 치수의 그려진 모양 (선 · 화살표 · 글자) 은 익명 블록(*D..)에 들어 있다
// 블록은 월드 좌표로 그려져 있다 - 맨 바깥이면 도면을 옮긴 만큼만 따라 옮긴다
void Emitter::emitDimension(const Entity& e, const Xform& x, const Entity* parent, int depth) {
    const std::string& t = e.type;
    Xform local;
    if (!parent) local.t = vec3{static_cast<float>(-stats.originX), static_cast<float>(-stats.originY), 0.0f};
    if (blocks.count(e.str(2))) expandBlock(e.str(2), x.then(local), e, depth);
    else noteSkipped("DIMENSION");
}

}  // namespace detail
}  // namespace lot_dxf
