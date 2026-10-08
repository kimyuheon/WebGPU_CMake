// DXF 읽기: 첫 훑기 (코드/값 쌍 -> 엔티티 · 블록 · 층 표, POLYLINE 묶음 접기, 원점 이동) 와 load().
// 펼치기는 Emitter (lot_dxf_emit*.cpp).
#include "dxf/lot_dxf_emit.h"

#include "lot_log.h"

#include <cmath>
#include <cstdio>

namespace lot_dxf {

using namespace detail;

LoadStats load(const std::string& text, LotGameObject::Map& objects, LotLayers& layers) {
    LoadStats stats;
    objects.clear();
    layers.clear();

    std::unordered_map<std::string, uint32_t> layerIds;
    layerIds.emplace("0", LotLayers::kDefault);

    // 측량 좌표처럼 원점에서 아주 먼 도면은 float 정밀도가 모자라 (5천만이면 4 단위 간격)
    // 확대하면 선이 떨린다. 도면 범위의 중심을 원점 근처로 옮겨 읽고, 옮긴 양은
    // stats.originX/Y 에 남긴다 (DXF 로 다시 쓸 때 더해 돌려놓는다).
    {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        if (headerPoint(text, "$EXTMIN", x0, y0) && headerPoint(text, "$EXTMAX", x1, y1)
            && x1 >= x0 && y1 >= y0) {
            const double cx = (x0 + x1) * 0.5, cy = (y0 + y1) * 0.5;
            if (std::fabs(cx) > 1e5 || std::fabs(cy) > 1e5) {
                stats.originX = std::round(cx / 1000.0) * 1000.0;
                stats.originY = std::round(cy / 1000.0) * 1000.0;
            }
        }
    }








    // ---- 1. 훑기: 엔티티와 블록 정의를 모은다 ----
    //
    // 블록 안의 것은 블록 좌표라 바로 그리면 안 된다 - INSERT 가 놓는 자리로 옮겨야 한다.
    // 그래서 먼저 다 모아 두고 (2) 에서 펼친다. POLYLINE ... VERTEX ... SEQEND 묶음은
    // 여기서 LWPOLYLINE 모양 하나로 접어 둔다 - 펼치는 쪽이 한 가지만 알면 된다.
    std::vector<Entity> modelEntities;
    std::unordered_map<std::string, Block> blocks;
    std::string section;
    std::string blockName;       // 지금 정의 중인 블록 (BLOCKS 섹션)
    bool inBlock = false;

    auto store = [&](Entity e) {
        if (section == "ENTITIES") {
            shiftEntity(e, stats.originX, stats.originY);
            modelEntities.push_back(std::move(e));
        }
        else if (section == "BLOCKS" && inBlock) blocks[blockName].entities.push_back(std::move(e));
    };

    Reader reader{text};
    int code = 0;
    std::string value;
    Entity current;
    bool inEntity = false;
    bool inPolyline = false;
    Entity polyline;             // 머리 + 접어 넣는 꼭짓점들
    bool polylineFirstVertex = true;

    auto finishEntity = [&]() {
        if (!inEntity) return;
        Entity& e = current;
        const std::string& t = e.type;

        if (t == "LAYER" && section == "TABLES") {
            const std::string name = e.str(2);
            if (name.empty()) return;
            const int aci = e.integer(62, 7);
            const vec3 color = aciColor(std::abs(aci));
            uint32_t id;
            if (name == "0") {
                id = LotLayers::kDefault;
            } else {
                id = layers.create(name, color);
                layerIds.emplace(name, id);
            }
            if (LotLayers::Layer* l = layers.find(id)) {
                l->color = color;
                l->linetype = linetypeByName(e.str(6));
                if (id != LotLayers::kDefault) {
                    l->visible = aci >= 0;            // 음수 = 꺼진 층 (DXF 관례)
                    l->locked = (e.integer(70) & 4) != 0;
                }
            }
            ++stats.layers;
            return;
        }
        if (section == "BLOCKS" && t == "BLOCK") {
            blockName = e.str(2);
            inBlock = !blockName.empty();
            if (inBlock) blocks[blockName].base = vec3{e.num(10), e.num(20), e.num(30)};
            return;
        }
        if (section == "BLOCKS" && t == "ENDBLK") { inBlock = false; return; }
        if (t.empty() || t == "SECTION" || t == "ENDSEC" || t == "EOF" || t == "SEQEND") return;
        if (section != "ENTITIES" && section != "BLOCKS") return;
        store(std::move(e));
    };

    while (reader.next(code, value)) {
        if (code == 0) {
            if (inPolyline) {
                if (current.type == "VERTEX" && polyline.type == "PMESH") {
                    // 메시 POLYLINE: 정점은 10/20/30 그대로, 폴리페이스 면 기록(128 만, 64 없음)은
                    // 71..74 (1 기준, 음수 = 숨은 모서리) 넷씩 seq 에 (없는 자리는 0)
                    const int vf = current.integer(70);
                    if ((vf & 128) && !(vf & 64)) {
                        for (int k = 71; k <= 74; ++k) polyline.seq.emplace_back(k, current.str(k, "0"));
                    } else {
                        polyline.values.emplace(10, current.str(10, "0"));
                        polyline.values.emplace(20, current.str(20, "0"));
                        polyline.values.emplace(30, current.str(30, "0"));
                    }
                    inEntity = false;
                } else if (current.type == "VERTEX") {
                    // 폴리페이스 메시의 면 기록(128 만, 64 없음)은 좌표가 아니다
                    const int vf = current.integer(70);
                    if (!((vf & 128) && !(vf & 64))) {
                        polyline.values.emplace(10, current.str(10, "0"));
                        polyline.values.emplace(20, current.str(20, "0"));
                        polyline.values.emplace(42, current.str(42, "0"));
                        if (polylineFirstVertex) polyline.values.emplace(38, current.str(30, "0"));
                        polylineFirstVertex = false;
                    }
                    inEntity = false;
                } else if (current.type == "POLYLINE") {
                    // 폴리페이스(64) · 폴리곤 메시(16) 는 선이 아니라 면이다 (네이티브 isMesh)
                    if (current.integer(70) & (16 | 64)) polyline.type = "PMESH";
                    for (const auto& kv : current.values) {
                        if (kv.first != 10 && kv.first != 20 && kv.first != 30) polyline.values.emplace(kv);
                    }
                    inEntity = false;
                }
                if (value == "SEQEND") {
                    inPolyline = false;
                    current = std::move(polyline);
                    inEntity = true;
                    finishEntity();
                    inEntity = false;
                    polyline = Entity{};
                }
            }
            finishEntity();

            if (value == "SECTION" || value == "ENDSEC") section.clear();
            if (value == "POLYLINE") {
                inPolyline = true;
                polyline = Entity{};
                polyline.type = "LWPOLYLINE";
                polylineFirstVertex = true;
            }
            current = Entity{};
            current.type = value;
            inEntity = true;
            if (value == "EOF") break;
        } else if (code == 2 && current.type == "SECTION") {
            section = value;              // HEADER / TABLES / BLOCKS / ENTITIES
            current.values.emplace(code, value);
        } else {
            current.values.emplace(code, value);
            if (current.type == "HATCH") current.seq.emplace_back(code, value);
        }
    }

    // ---- 2. 펼치기 ----
    //
    // parent 는 이 엔티티를 놓은 INSERT (맨 바깥이면 nullptr). 블록 안에서 층 "0" 인 것은
    // 삽입한 층을, 색 BYBLOCK(0) 은 삽입의 색을 따른다 (AutoCAD 규칙).



    Emitter em{stats, objects, layers, layerIds, blocks};
    for (const Entity& e : modelEntities) em.emit(e, Xform{}, nullptr, 0);
    const int inserts = em.inserts;

    const int total = stats.lines + stats.circles + stats.arcs + stats.polylines
                    + stats.texts + stats.splines + stats.hatches + stats.ellipses + stats.leaders
                    + stats.meshes;
    if (total == 0) {
        stats.error = "dxf: no drawable entities found (is this an ASCII DXF?)";
        return stats;
    }
    LOT_LOG("dxf: " << stats.lines << " lines, " << stats.circles << " circles, "
            << stats.arcs << " arcs, " << stats.polylines << " polylines, "
            << stats.texts << " texts, " << stats.splines << " splines (approx), "
            << stats.layers << " layers, " << inserts << " inserts, " << blocks.size() << " blocks, "
            << stats.hatches << " hatches, " << stats.ellipses << " ellipses, " << stats.leaders << " leaders, "
            << stats.tables << " tables, " << stats.meshes << " meshes (" << stats.faces << " triangles)"
            << (stats.skipped ? ", skipped " + std::to_string(stats.skipped) + " ("
                                + stats.skippedKinds + ")" : ""));
    if (stats.originX != 0.0 || stats.originY != 0.0) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "dxf: far from the origin - shifted by (%.0f, %.0f)", -stats.originX, -stats.originY);
        LOT_LOG(buf);
    }
    return stats;
}

}  // namespace lot_dxf
