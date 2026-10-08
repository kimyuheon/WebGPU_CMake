// DXF 펼치기 - 3DFACE / MESH / 폴리페이스 · 폴리곤 메시 (네이티브와 같이 같은 층 · 색끼리 한 객체).
#include "dxf/lot_dxf_emit.h"

#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_dxf {
namespace detail {

// 면 - 3D 로 내보낸 DXF(3ds Max · Rhino 등)는 3DFACE 가 곧 메시다. 넷째 점이 셋째와 같으면 삼각형.
void Emitter::emit3dFace(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    std::vector<vec3> v;
    for (int k = 0; k < 4; ++k) {
        if (e.has(10 + k)) v.push_back(vec3{e.num(10 + k), e.num(20 + k), e.num(30 + k)});
    }
    if (v.size() == 4 && dot(v[3] - v[2], v[3] - v[2]) < 1e-24f) v.pop_back();
    std::vector<int> idx;
    for (int k = 0; k < static_cast<int>(v.size()); ++k) idx.push_back(k);
    std::vector<vec3> tris;
    appendFace(tris, v, idx, x);
    if (tris.empty()) noteSkipped("3DFACE"); else addMesh(tris, e);
}

// AcDbSubDMesh: 정점 10/20/30, 면 목록 93 개의 90 값 [n, i…] (0 기준). 세분(91)은 무시하고
// 제어 메시를 그대로 그린다 (네이티브와 같다).
void Emitter::emitMesh(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    const std::vector<float> vx = e.all(10), vy = e.all(20), vz = e.all(30);
    std::vector<vec3> v;
    for (size_t i = 0; i < std::min(vx.size(), vy.size()); ++i) v.push_back(vec3{vx[i], vy[i], i < vz.size() ? vz[i] : 0.0f});
    std::vector<int> fl;
    for (float f : e.all(90)) fl.push_back(static_cast<int>(f));
    const int m = e.integer(93, 0);
    const size_t len = std::min(fl.size(), m > 0 ? static_cast<size_t>(m) : fl.size());
    std::vector<vec3> tris;
    for (size_t i = 0; i < len;) {
        const int n = fl[i];
        if (n <= 0 || i + 1 + n > len) break;
        appendFace(tris, v, std::vector<int>(fl.begin() + i + 1, fl.begin() + i + 1 + n), x);
        i += 1 + n;
    }
    if (tris.empty()) noteSkipped("MESH(no faces)"); else addMesh(tris, e);
}

// 메시 POLYLINE - 폴리페이스(64): 면 기록 71..74 (1 기준, 음수 = 숨은 모서리, 0 = 없음).
// 폴리곤 메시(16): M(71) x N(72) 격자, 70 & 1 / & 32 = M / N 방향 닫힘.
void Emitter::emitPolyMesh(const Entity& e, const Xform& x, const Entity* /*parent*/, int /*depth*/) {
    const std::vector<float> vx = e.all(10), vy = e.all(20), vz = e.all(30);
    std::vector<vec3> v;
    for (size_t i = 0; i < std::min(vx.size(), vy.size()); ++i) v.push_back(vec3{vx[i], vy[i], i < vz.size() ? vz[i] : 0.0f});
    const int flags = e.integer(70);
    std::vector<vec3> tris;
    if (flags & 64) {
        for (size_t i = 0; i + 3 < e.seq.size(); i += 4) {
            std::vector<int> idx;
            for (size_t k = 0; k < 4; ++k) {
                const int f = toInt(e.seq[i + k].second);
                if (f != 0) idx.push_back(std::abs(f) - 1);
            }
            appendFace(tris, v, idx, x);
        }
    } else {
        const int mM = e.integer(71), mN = e.integer(72);
        if (mM > 0 && mN > 0 && static_cast<int>(v.size()) >= mM * mN) {
            const bool closeM = (flags & 1) != 0, closeN = (flags & 32) != 0;
            const int mEnd = closeM ? mM : mM - 1, nEnd = closeN ? mN : mN - 1;
            for (int i = 0; i < mEnd; ++i) {
                for (int j = 0; j < nEnd; ++j) {
                    const int i1 = (i + 1) % mM, j1 = (j + 1) % mN;
                    appendFace(tris, v, {i * mN + j, i1 * mN + j, i1 * mN + j1, i * mN + j1}, x);
                }
            }
        }
    }
    if (tris.empty()) noteSkipped("POLYLINE(mesh)"); else addMesh(tris, e);
}

}  // namespace detail
}  // namespace lot_dxf
