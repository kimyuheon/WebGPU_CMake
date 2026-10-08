#pragma once

// DXF 펼치기 - 첫 훑기(lot_dxf_read)가 모은 엔티티를 객체로 만든다. load() 안의 상태를 한 묶음으로.
#include "dxf/lot_dxf_internal.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lot_dxf {
namespace detail {

struct Emitter {
    LoadStats& stats;
    LotGameObject::Map& objects;
    LotLayers& layers;
    std::unordered_map<std::string, uint32_t>& layerIds;    // 층 이름 -> id
    const std::unordered_map<std::string, Block>& blocks;
    int inserts = 0;

    // 메시 조각 (월드 삼각형, 세 점씩). 바로 앞 객체가 같은 층 · 색의 메시면 거기에 붙인다 -
    // 3ds Max 같은 것은 면마다 3DFACE 를 써서 수만 객체가 되기 때문 (네이티브 import 와 같다).
    // 모델은 부르는 쪽이 stats.meshTriangles 로 만든다.
    struct MeshKey {
        uint32_t layer;
        bool byLayer;
        vec3 color;
    };
    MeshKey lastMeshKey{};
    size_t lastMeshObjects = static_cast<size_t>(-1);   // 그때의 objects.size() - 그새 다른 것이 생겼으면 다르다

    void emit(const Entity& src, const Xform& x, const Entity* parent, int depth);
    void expandBlock(const std::string& name, const Xform& placed, const Entity& owner, int depth);

    // 객체 추가 (lot_dxf_emit.cpp)
    void noteSkipped(const std::string& type);
    uint32_t layerOf(const Entity& e);
    vec3 colorOf(const Entity& e, uint32_t layerId, bool& byLayer);
    void addSketch(std::vector<vec3> pts, bool closed, const Entity& e, const LotGameObject::Curve* curve);
    void addText(const std::string& content, const Entity& e, const vec3& at, const vec3& right, float height,
                 int hAlign, int vAlign);
    void addHatch(std::shared_ptr<lot_hatch::HatchData> h, const vec3& o, const vec3& right, const vec3& up,
                  const Entity& e);
    void addMesh(const std::vector<vec3>& tris, const Entity& e);
    static void appendFace(std::vector<vec3>& tris, const std::vector<vec3>& v, const std::vector<int>& idx,
                           const Xform& x);

    // 종류별 (x = 블록 변환, parent = 놓은 INSERT, depth = 블록 깊이)
    // lot_dxf_emit.cpp
    void emitTable(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitInsert(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitDimension(const Entity& e, const Xform& x, const Entity* parent, int depth);
    // lot_dxf_emit_curves.cpp
    void emitLine(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitCircle(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitLwPolyline(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitSpline(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitEllipse(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitLeader(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitSolid(const Entity& e, const Xform& x, const Entity* parent, int depth);
    // lot_dxf_emit_text.cpp
    void emitText(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitMText(const Entity& e, const Xform& x, const Entity* parent, int depth);
    // lot_dxf_emit_hatch.cpp
    void emitHatch(const Entity& e, const Xform& x, const Entity* parent, int depth);
    // lot_dxf_emit_mesh.cpp
    void emit3dFace(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitMesh(const Entity& e, const Xform& x, const Entity* parent, int depth);
    void emitPolyMesh(const Entity& e, const Xform& x, const Entity* parent, int depth);
};

}  // namespace detail
}  // namespace lot_dxf
