#include "lot_dxf.h"
#include "lot_linetype.h"
#include "lot_log.h"
#include "lot_sketch_tool.h"  // tessellateArc

#include <cmath>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <vector>

namespace lot_dxf {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

// ---- AutoCAD Color Index -> RGB ----
//
// 1..9 는 잘 알려진 고정 색, 10..249 는 24 색상 × (채도 2 × 명도 5), 250..255 는 회색.
// 정확한 표를 통째로 박는 대신 그 구조를 그대로 계산한다 - 몇 단계 밝기가 원본과
// 미세하게 다를 수 있지만 도면을 보는 데는 문제가 없다.
vec3 hsvToRgb(float h, float s, float v) {
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return vec3{r + m, g + m, b + m};
}

vec3 aciColor(int index) {
    static const vec3 kBasic[10] = {
        {1.0f, 1.0f, 1.0f},  // 0 ByBlock - 흰색으로 본다
        {1.0f, 0.0f, 0.0f},  // 1 red
        {1.0f, 1.0f, 0.0f},  // 2 yellow
        {0.0f, 1.0f, 0.0f},  // 3 green
        {0.0f, 1.0f, 1.0f},  // 4 cyan
        {0.0f, 0.0f, 1.0f},  // 5 blue
        {1.0f, 0.0f, 1.0f},  // 6 magenta
        {0.9f, 0.9f, 0.9f},  // 7 white/black - 어두운 배경이라 밝게
        {0.5f, 0.5f, 0.5f},  // 8 dark grey
        {0.75f, 0.75f, 0.75f},  // 9 light grey
    };
    if (index >= 0 && index < 10) return kBasic[index];
    if (index >= 250 && index <= 255) {
        const float g = (51 + (index - 250) * 41) / 255.0f;
        return vec3{g, g, g};
    }
    if (index >= 10 && index <= 249) {
        const int i = index - 10;
        const float hue = static_cast<float>(i / 10) * 15.0f;  // 24 색상
        const int sub = i % 10;
        const float sat = (sub % 2 == 0) ? 1.0f : 0.5f;
        const float val = 1.0f - 0.18f * static_cast<float>(sub / 2);
        return hsvToRgb(hue, sat, val);
    }
    return vec3{0.9f, 0.9f, 0.9f};
}

// ---- 코드/값 쌍 읽기 ----
//
// DXF 는 줄 두 개가 한 쌍이다: 그룹 코드, 값. 코드 앞뒤 공백과 CR 을 털어낸다.
struct Reader {
    const std::string& text;
    size_t pos = 0;

    bool next(int& code, std::string& value) {
        std::string first, second;
        if (!line(first) || !line(second)) return false;
        code = std::atoi(trim(first).c_str());
        value = trim(second);
        return true;
    }

private:
    bool line(std::string& out) {
        if (pos >= text.size()) return false;
        const size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            out.assign(text, pos, text.size() - pos);
            pos = text.size();
        } else {
            out.assign(text, pos, end - pos);
            pos = end + 1;
        }
        return true;
    }
    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
        return s.substr(a, b - a);
    }
};

float toFloat(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
int toInt(const std::string& s) { return std::atoi(s.c_str()); }

// 엔티티 하나 분량의 코드/값. 같은 코드가 여러 번 오는 것(폴리선 좌표)도 담는다.
struct Entity {
    std::string type;
    std::multimap<int, std::string> values;

    std::string str(int code, const std::string& fallback = "") const {
        auto it = values.find(code);
        return it == values.end() ? fallback : it->second;
    }
    float num(int code, float fallback = 0.0f) const {
        auto it = values.find(code);
        return it == values.end() ? fallback : toFloat(it->second);
    }
    int integer(int code, int fallback = 0) const {
        auto it = values.find(code);
        return it == values.end() ? fallback : toInt(it->second);
    }
    bool has(int code) const { return values.find(code) != values.end(); }
    std::vector<float> all(int code) const {
        std::vector<float> out;
        auto range = values.equal_range(code);
        for (auto it = range.first; it != range.second; ++it) out.push_back(toFloat(it->second));
        return out;
    }
};

// 선종류 이름 -> 우리 id (표준 8종). 모르는 이름은 실선.
uint32_t linetypeByName(const std::string& name) {
    for (const lot_linetype::Definition& d : lot_linetype::standard()) {
        if (name == d.name) return d.id;
    }
    // DXF 는 대문자로 적는 일이 많다 (DASHED, HIDDEN, CENTER…)
    std::string upper;
    for (char c : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const lot_linetype::Definition& d : lot_linetype::standard()) {
        std::string dn;
        for (const char* p = d.name; *p; ++p) dn += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        if (upper == dn) return d.id;
    }
    return lot_linetype::kContinuous;
}

// 폴리선의 bulge (코드 42) - 두 점 사이가 직선이 아니라 호다.
// bulge = tan(사잇각 / 4). 부호는 방향 (양수 = 반시계).
void appendBulgeArc(std::vector<vec3>& out, const vec3& a, const vec3& b, float bulge) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float chord = std::sqrt(dx * dx + dy * dy);
    if (chord < 1e-9f || std::fabs(bulge) < 1e-9f) return;

    const float theta = 4.0f * std::atan(bulge);      // 사잇각
    const float radius = chord / (2.0f * std::sin(std::fabs(theta) / 2.0f));
    // 현의 중점에서 수직으로 sagitta 만큼 떨어진 곳이 중심
    const float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    const float h = std::sqrt(std::fmax(radius * radius - chord * chord * 0.25f, 0.0f));
    const float sign = (bulge > 0.0f) ? 1.0f : -1.0f;
    const float dir = (std::fabs(theta) > kPi) ? -1.0f : 1.0f;  // 반원보다 크면 중심이 반대쪽
    const float cx = mx - dy / chord * h * sign * dir;
    const float cy = my + dx / chord * h * sign * dir;

    const float a0 = std::atan2(a.y - cy, a.x - cx);
    int segments = static_cast<int>(std::fabs(theta) / (kPi / 12.0f)) + 2;  // 15도쯤마다
    for (int i = 1; i < segments; ++i) {
        const float t = a0 + theta * static_cast<float>(i) / static_cast<float>(segments);
        out.push_back(vec3{cx + radius * std::cos(t), cy + radius * std::sin(t), a.z});
    }
}

}  // namespace

LoadStats load(const std::string& text, LotGameObject::Map& objects, LotLayers& layers) {
    LoadStats stats;
    objects.clear();
    layers.clear();

    std::unordered_map<std::string, uint32_t> layerIds;
    layerIds.emplace("0", LotLayers::kDefault);
    std::unordered_map<std::string, vec3> layerColors;

    auto noteSkipped = [&](const std::string& type) {
        ++stats.skipped;
        if (stats.skippedKinds.find(type) == std::string::npos) {
            if (!stats.skippedKinds.empty()) stats.skippedKinds += ", ";
            stats.skippedKinds += type;
        }
    };

    auto layerOf = [&](const Entity& e) {
        const std::string name = e.str(8, "0");
        auto it = layerIds.find(name);
        if (it != layerIds.end()) return it->second;
        const uint32_t id = layers.create(name, vec3{0.8f, 0.8f, 0.85f});
        layerIds.emplace(name, id);
        return id;
    };

    // 엔티티 색: 62 가 있으면 그 색, 없거나 256 이면 층 따름.
    auto colorOf = [&](const Entity& e, uint32_t layerId, bool& byLayer) {
        const int aci = e.integer(62, 256);
        if (aci == 256 || aci <= 0) {
            byLayer = true;
            const LotLayers::Layer* l = layers.find(layerId);
            return l ? l->color : vec3{0.9f, 0.9f, 0.9f};
        }
        byLayer = false;
        return aciColor(aci);
    };

    auto addSketch = [&](std::vector<vec3> pts, bool closed, const Entity& e,
                         const LotGameObject::Curve* curve) {
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
    };

    // ---- 훑기 ----
    Reader reader{text};
    int code = 0;
    std::string value;
    std::string section;
    Entity current;
    bool inEntity = false;
    // POLYLINE 은 뒤따르는 VERTEX 들을 SEQEND 까지 모은다
    bool inPolyline = false;
    Entity polylineHeader;
    std::vector<vec3> polylinePoints;
    std::vector<float> polylineBulges;

    auto finishEntity = [&]() {
        if (!inEntity) return;
        const Entity& e = current;
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
        if (section != "ENTITIES" && section != "BLOCKS") return;

        if (t == "LINE") {
            addSketch({vec3{e.num(10), e.num(20), e.num(30)},
                       vec3{e.num(11), e.num(21), e.num(31)}}, false, e, nullptr);
            ++stats.lines;
        } else if (t == "CIRCLE") {
            LotGameObject::Curve c;
            c.kind = LotGameObject::Curve::Kind::Circle;
            c.center = vec3{e.num(10), e.num(20), e.num(30)};
            c.radius = e.num(40, 1.0f);
            c.right = vec3{1.0f, 0.0f, 0.0f};
            c.up = vec3{0.0f, 1.0f, 0.0f};
            c.start = 0.0f;
            c.end = 2.0f * kPi;
            addSketch(tessellateArc(c.center, c.radius, c.right, c.up, 0.0f, 2.0f * kPi, false),
                      true, e, &c);
            ++stats.circles;
        } else if (t == "ARC") {
            LotGameObject::Curve c;
            c.kind = LotGameObject::Curve::Kind::Arc;
            c.center = vec3{e.num(10), e.num(20), e.num(30)};
            c.radius = e.num(40, 1.0f);
            c.right = vec3{1.0f, 0.0f, 0.0f};
            c.up = vec3{0.0f, 1.0f, 0.0f};
            c.start = e.num(50) * kDegToRad;
            c.end = e.num(51) * kDegToRad;
            if (c.end <= c.start) c.end += 2.0f * kPi;  // DXF 호는 항상 반시계
            addSketch(tessellateArc(c.center, c.radius, c.right, c.up, c.start, c.end, true),
                      false, e, &c);
            ++stats.arcs;
        } else if (t == "LWPOLYLINE") {
            const std::vector<float> xs = e.all(10), ys = e.all(20);
            const std::vector<float> bulges = e.all(42);
            const float z = e.num(38);
            const bool closed = (e.integer(70) & 1) != 0;
            std::vector<vec3> pts;
            const size_t n = std::min(xs.size(), ys.size());
            for (size_t i = 0; i < n; ++i) {
                pts.push_back(vec3{xs[i], ys[i], z});
                // bulge 는 '이 점에서 다음 점까지' 다. 개수가 점과 같지 않은 파일도 있다.
                const size_t last = (i + 1 < n) ? i + 1 : 0;
                if ((i + 1 < n || closed) && i < bulges.size() && std::fabs(bulges[i]) > 1e-9f) {
                    appendBulgeArc(pts, vec3{xs[i], ys[i], z}, vec3{xs[last], ys[last], z}, bulges[i]);
                }
            }
            addSketch(std::move(pts), closed, e, nullptr);
            ++stats.polylines;
        } else if (t == "TEXT") {
            const std::string content = e.str(1);
            if (content.empty()) { noteSkipped("TEXT(empty)"); return; }
            const uint32_t layerId = layerOf(e);
            bool byLayer = false;
            const vec3 color = colorOf(e, layerId, byLayer);
            const float angle = e.num(50) * kDegToRad;
            auto obj = LotGameObject::createGameObject();
            obj.transform.translation = vec3{e.num(10), e.num(20), e.num(30)};
            obj.color = color;
            obj.colorByLayer = byLayer;
            obj.layer = layerId;
            obj.text.valid = true;
            obj.text.content = content;
            obj.text.height = e.num(40, 1.0f);
            obj.text.right = vec3{std::cos(angle), std::sin(angle), 0.0f};
            obj.text.up = vec3{-std::sin(angle), std::cos(angle), 0.0f};
            obj.text.hAlign = 0;
            obj.text.vAlign = 0;  // DXF 의 기본은 왼쪽 아래(기준선)
            objects.emplace(obj.getId(), std::move(obj));
            ++stats.texts;
        } else if (t == "SPLINE") {
            // 제어점을 이어 근사한다 (진짜 NURBS 평가는 아직). 맞춤점이 있으면 그걸 쓴다.
            const std::vector<float> fx = e.all(11), fy = e.all(21);
            const std::vector<float> cx = e.all(10), cy = e.all(20);
            const bool useFit = fx.size() >= 2 && fx.size() == fy.size();
            const std::vector<float>& xs = useFit ? fx : cx;
            const std::vector<float>& ys = useFit ? fy : cy;
            std::vector<vec3> pts;
            const size_t n = std::min(xs.size(), ys.size());
            for (size_t i = 0; i < n; ++i) pts.push_back(vec3{xs[i], ys[i], 0.0f});
            if (pts.size() >= 2) {
                addSketch(std::move(pts), (e.integer(70) & 1) != 0, e, nullptr);
                ++stats.splines;
            } else {
                noteSkipped("SPLINE");
            }
        } else if (t == "SOLID" || t == "TRACE") {
            // 채운 사각형 - 테두리만 그린다
            std::vector<vec3> pts{vec3{e.num(10), e.num(20), e.num(30)},
                                  vec3{e.num(11), e.num(21), e.num(31)},
                                  vec3{e.num(13), e.num(23), e.num(33)},
                                  vec3{e.num(12), e.num(22), e.num(32)}};
            addSketch(std::move(pts), true, e, nullptr);
            ++stats.polylines;
        } else if (t == "VERTEX" || t == "SEQEND" || t == "POLYLINE" || t == "ENDBLK"
                   || t == "BLOCK" || t == "TABLE" || t == "ENDTAB" || t == "ENDSEC"
                   || t == "VPORT" || t == "LTYPE" || t == "STYLE" || t == "APPID"
                   || t == "DIMSTYLE" || t == "UCS" || t == "VIEW" || t == "CLASS"
                   || t == "DICTIONARY" || t == "XRECORD" || t == "VISUALSTYLE"
                   || t == "SCALE" || t == "DICTIONARYVAR" || t == "LAYOUT"
                   || t == "MLINESTYLE" || t == "PLOTSETTINGS" || t == "TABLESTYLE") {
            // 구조용 항목 - 세지 않는다
        } else if (!t.empty()) {
            noteSkipped(t);
        }
    };

    while (reader.next(code, value)) {
        if (code == 0) {
            // POLYLINE ... VERTEX ... SEQEND 묶음 처리
            if (inPolyline && current.type == "VERTEX") {
                polylinePoints.push_back(vec3{current.num(10), current.num(20), current.num(30)});
                polylineBulges.push_back(current.num(42));
            }
            if (inPolyline && value == "SEQEND") {
                std::vector<vec3> pts;
                for (size_t i = 0; i < polylinePoints.size(); ++i) {
                    pts.push_back(polylinePoints[i]);
                    const bool closed = (polylineHeader.integer(70) & 1) != 0;
                    const size_t last = (i + 1 < polylinePoints.size()) ? i + 1 : 0;
                    if ((i + 1 < polylinePoints.size() || closed) && i < polylineBulges.size()
                        && std::fabs(polylineBulges[i]) > 1e-9f) {
                        appendBulgeArc(pts, polylinePoints[i], polylinePoints[last], polylineBulges[i]);
                    }
                }
                addSketch(std::move(pts), (polylineHeader.integer(70) & 1) != 0, polylineHeader, nullptr);
                ++stats.polylines;
                inPolyline = false;
                polylinePoints.clear();
                polylineBulges.clear();
            } else if (!inPolyline || current.type != "VERTEX") {
                finishEntity();
            }

            if (value == "SECTION") {
                section.clear();
            } else if (value == "ENDSEC") {
                section.clear();
            } else if (value == "POLYLINE") {
                inPolyline = true;
                polylineHeader = Entity{};
                polylineHeader.type = "POLYLINE";
                polylinePoints.clear();
                polylineBulges.clear();
            }
            current = Entity{};
            current.type = value;
            inEntity = true;
            if (value == "EOF") break;
        } else if (code == 2 && current.type == "SECTION") {
            section = value;              // HEADER / TABLES / BLOCKS / ENTITIES
            current.values.emplace(code, value);
        } else {
            if (inPolyline && current.type == "POLYLINE") polylineHeader.values.emplace(code, value);
            current.values.emplace(code, value);
        }
    }

    const int total = stats.lines + stats.circles + stats.arcs + stats.polylines
                    + stats.texts + stats.splines;
    if (total == 0) {
        stats.error = "dxf: no drawable entities found (is this an ASCII DXF?)";
        return stats;
    }
    LOT_LOG("dxf: " << stats.lines << " lines, " << stats.circles << " circles, "
            << stats.arcs << " arcs, " << stats.polylines << " polylines, "
            << stats.texts << " texts, " << stats.splines << " splines (approx), "
            << stats.layers << " layers"
            << (stats.skipped ? ", skipped " + std::to_string(stats.skipped) + " ("
                                + stats.skippedKinds + ")" : ""));
    return stats;
}

}  // namespace lot_dxf
