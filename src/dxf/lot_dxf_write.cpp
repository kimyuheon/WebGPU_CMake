// DXF 쓰기 (R12 ASCII) - 선 · 원 · 호 · 폴리선 · 문자, 치수는 선과 문자로 풀어서.
#include "dxf/lot_dxf_internal.h"

#include "lot_dimension.h"
#include "lot_linetype.h"
#include "lot_log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lot_dxf {

using namespace detail;

namespace {

// DXF 는 줄 두 개가 한 쌍이다: 그룹 코드, 값. 쓰는 쪽도 그 쌍만 있으면 된다.
struct Writer {
    std::string out;
    double originX = 0.0, originY = 0.0;   // 읽을 때 옮긴 만큼 되돌린다 (LoadStats::originX)

    void pair(int code, const std::string& v) {
        out += std::to_string(code);
        out += '\n';
        out += v;
        out += '\n';
    }
    void pair(int code, const char* v) { pair(code, std::string(v)); }
    void integer(int code, int v) { pair(code, std::to_string(v)); }
    void real(int code, float v) { pair(code, number(v)); }
    void point(int base, const vec3& p) {
        pair(base, number(static_cast<double>(p.x) + originX));
        pair(base + 10, number(static_cast<double>(p.y) + originY));
        real(base + 20, p.z);
    }

    // 고정 소수점. %g 는 큰 좌표에서 지수 표기(1e+06)로 새는데 그걸 못 읽는
    // 프로그램이 있다. 뒤따르는 0 만 털어 파일이 붓지 않게 한다.
    static std::string number(double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6f", v);
        std::string s(buf);
        const size_t dot = s.find('.');
        if (dot != std::string::npos) {
            size_t end = s.size();
            while (end > dot + 2 && s[end - 1] == '0') --end;
            s.erase(end);
        }
        return s;
    }
};

// RGB -> 가장 가까운 ACI. 읽을 때 쓰는 aciColor 를 그대로 되짚으므로,
// DXF 에서 읽어 온 색은 같은 번호로 되돌아간다 (왕복이 정확하다).
int aciFromColor(const vec3& c) {
    int best = 7;
    float bestDistance = 1e30f;
    for (int i = 1; i <= 255; ++i) {
        const vec3 a = aciColor(i);
        const vec3 d = a - c;
        const float distance = dot(d, d);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

float lengthOf(const vec3& v) { return std::sqrt(dot(v, v)); }

// 0 .. 360 으로 접는다 (DXF 호 각도 규약).
float degrees360(float radians) {
    float d = radians / kDegToRad;
    while (d < 0.0f) d += 360.0f;
    while (d >= 360.0f) d -= 360.0f;
    return d;
}

}  // namespace

std::string save(const LotGameObject::Map& objects, const LotLayers& layers,
                 float linetypeScale, SaveStats* outStats, double originX, double originY) {
    SaveStats st;
    Writer w;
    w.originX = originX;
    w.originY = originY;

    // 층은 이름으로 잇는다. 이름은 손대지 않는다 - R12 는 대문자만 허용하지만
    // 요즘 프로그램은 다 받고, 여기서 바꾸면 한글 층 이름이 왕복하지 못한다.
    std::unordered_map<uint32_t, std::string> layerNames;
    for (const LotLayers::Layer* l : layers.all()) layerNames.emplace(l->id, l->name);
    auto layerNameOf = [&](uint32_t id) -> std::string {
        auto it = layerNames.find(id);
        return it == layerNames.end() ? std::string("0") : it->second;
    };

    // 범위 ($EXTMIN/$EXTMAX). 읽는 쪽이 전체 보기를 바로 잡을 수 있게.
    vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool anyPoint = false;
    auto grow = [&](const vec3& p) {
        anyPoint = true;
        lo = vec3{std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z)};
        hi = vec3{std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z)};
    };

    // ---- 엔티티 본문을 먼저 만든다 (헤더에 적을 범위를 알아야 하므로) ----
    Writer body;

    auto attributes = [&](const char* type, const LotGameObject& o) {
        body.pair(0, type);
        body.pair(8, layerNameOf(o.layer));
        if (!o.colorByLayer) body.integer(62, aciFromColor(o.color));
        if (o.linetype != lot_linetype::kByLayer) body.pair(6, lot_linetype::name(o.linetype));
    };

    auto writeLine = [&](const LotGameObject& o, const vec3& a, const vec3& b) {
        attributes("LINE", o);
        body.point(10, a);
        body.point(11, b);
        grow(a);
        grow(b);
    };

    auto writeText = [&](const LotGameObject& o, const std::string& content, const vec3& origin,
                         const vec3& right, const vec3& up, float height, int hAlign, int vAlign) {
        if (content.empty()) return;
        attributes("TEXT", o);
        body.point(10, origin);
        body.real(40, height);
        body.pair(1, content);
        body.real(50, degrees360(std::atan2(right.y, right.x)));
        // 우리 vAlign: 0 기준선 · 1 가운데 · 2 위 -> DXF 73: 0 기준선 · 2 가운데 · 3 위
        const int dxfV = (vAlign == 1) ? 2 : (vAlign == 2) ? 3 : 0;
        if (hAlign != 0 || dxfV != 0) {
            body.integer(72, hAlign);
            body.integer(73, dxfV);
            body.point(11, origin);  // 맞춤점이 있으면 그게 진짜 기준점이다
        }
        grow(origin);
        grow(origin + right * (height * static_cast<float>(content.size())));
        grow(origin - up * height);
    };

    // 폴리선. 모든 점의 z 가 같으면 2D (고도 하나), 아니면 3D 폴리선으로.
    auto writePolyline = [&](const LotGameObject& o, const std::vector<vec3>& pts, bool closed) {
        bool flat = true;
        for (const vec3& p : pts) {
            if (std::fabs(p.z - pts[0].z) > 1e-6f) { flat = false; break; }
        }
        attributes("POLYLINE", o);
        body.integer(66, 1);                 // 정점이 뒤따른다 (R12 에서는 필수)
        body.integer(70, (closed ? 1 : 0) | (flat ? 0 : 8));
        body.real(10, 0.0f);
        body.real(20, 0.0f);
        body.real(30, flat ? pts[0].z : 0.0f);
        for (const vec3& p : pts) {
            body.pair(0, "VERTEX");
            body.pair(8, layerNameOf(o.layer));
            body.point(10, p);
            if (!flat) body.integer(70, 32);  // 3D 폴리선의 정점
            grow(p);
        }
        body.pair(0, "SEQEND");
        body.pair(8, layerNameOf(o.layer));
    };

    // id 순으로 쓴다 - 맵은 순서가 없어 그냥 돌면 저장할 때마다 파일이 뒤바뀐다.
    std::vector<LotGameObject::id_t> ids;
    ids.reserve(objects.size());
    for (const auto& entry : objects) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());

    for (const LotGameObject::id_t id : ids) {
        const LotGameObject& o = objects.at(id);
        const mat4 m = o.transform.mat4Transform();

        if (o.text.valid) {
            writeText(o, o.text.content, o.transform.translation,
                      normalize(rotate(o.transform.rotation, o.text.right)),
                      normalize(rotate(o.transform.rotation, o.text.up)),
                      o.text.height, o.text.hAlign, o.text.vAlign);
            ++st.texts;
            continue;
        }

        if (o.dim.valid) {
            // DIMENSION 엔티티는 그려진 모양을 담은 블록을 달고 다녀야 한다.
            // 보이는 것을 그대로 선과 글자로 풀어 쓴다 (측정값은 글자로 남는다).
            const lot_dim::Geometry g = lot_dim::build(o.dim, m, nullptr);
            for (const std::pair<vec3, vec3>& seg : g.segments) writeLine(o, seg.first, seg.second);
            writeText(o, g.text, g.textOrigin, g.textRight, g.textUp, g.textHeight, 1, 1);
            ++st.dimensions;
            continue;
        }

        if (o.points.size() < 2) {
            ++st.skipped;   // 메시 (큐브 · OBJ) - 2D 도면 엔티티가 아니다
            continue;
        }

        // 원/호는 평면에 누운 채 모양이 안 망가졌을 때만 진짜 CIRCLE/ARC 로 나간다.
        // 기울었거나 축마다 다르게 늘어났으면 타원이 되는데, 그건 잘라 둔 점으로 쓴다.
        if (o.curve.kind != LotGameObject::Curve::Kind::None) {
            const vec3 center = transformPoint(m, o.curve.center);
            const vec3 right = transformPoint(m, o.curve.center + o.curve.right * o.curve.radius) - center;
            const vec3 up = transformPoint(m, o.curve.center + o.curve.up * o.curve.radius) - center;
            const float rr = lengthOf(right), ru = lengthOf(up);
            const float tolerance = 1e-4f * std::fmax(rr, 1.0f);
            if (rr > 1e-6f && std::fabs(rr - ru) < tolerance
                && std::fabs(right.z) < tolerance && std::fabs(up.z) < tolerance) {
                if (o.curve.kind == LotGameObject::Curve::Kind::Circle) {
                    attributes("CIRCLE", o);
                    body.point(10, center);
                    body.real(40, rr);
                    ++st.circles;
                } else {
                    const vec3 a = right * std::cos(o.curve.start) + up * std::sin(o.curve.start);
                    const vec3 b = right * std::cos(o.curve.end) + up * std::sin(o.curve.end);
                    float start = degrees360(std::atan2(a.y, a.x));
                    float end = degrees360(std::atan2(b.y, b.x));
                    // DXF 호는 언제나 반시계다. 우리 호가 시계 방향이면 양 끝을 바꾼다.
                    if (right.x * up.y - right.y * up.x < 0.0f) std::swap(start, end);
                    attributes("ARC", o);
                    body.point(10, center);
                    body.real(40, rr);
                    body.real(50, start);
                    body.real(51, end);
                    ++st.arcs;
                }
                grow(center - vec3{rr, rr, 0.0f});
                grow(center + vec3{rr, rr, 0.0f});
                continue;
            }
        }

        std::vector<vec3> pts;
        pts.reserve(o.points.size());
        for (const vec3& p : o.points) pts.push_back(transformPoint(m, p));

        if (pts.size() == 2 && !o.closed) {
            writeLine(o, pts[0], pts[1]);
            ++st.lines;
        } else {
            writePolyline(o, pts, o.closed);
            ++st.polylines;
        }
    }

    if (!anyPoint) { lo = vec3{0.0f, 0.0f, 0.0f}; hi = vec3{0.0f, 0.0f, 0.0f}; }

    // ---- HEADER ----
    w.pair(0, "SECTION");
    w.pair(2, "HEADER");
    w.pair(9, "$ACADVER");   w.pair(1, "AC1009");
    w.pair(9, "$INSBASE");   w.point(10, vec3{0.0f, 0.0f, 0.0f});
    w.pair(9, "$EXTMIN");    w.point(10, lo);
    w.pair(9, "$EXTMAX");    w.point(10, hi);
    w.pair(9, "$LTSCALE");   w.real(40, linetypeScale);
    w.pair(9, "$CLAYER");    w.pair(8, layerNameOf(layers.current()));
    w.pair(0, "ENDSEC");

    // ---- TABLES: LTYPE, LAYER ----
    w.pair(0, "SECTION");
    w.pair(2, "TABLES");

    const std::vector<lot_linetype::Definition>& definitions = lot_linetype::standard();
    w.pair(0, "TABLE");
    w.pair(2, "LTYPE");
    w.integer(70, static_cast<int>(definitions.size()));
    for (const lot_linetype::Definition& d : definitions) {
        w.pair(0, "LTYPE");
        w.pair(2, d.name);
        w.integer(70, 0);
        w.pair(3, d.name);
        w.integer(72, 65);                      // 'A' - 정렬 방식, 언제나 이것
        w.integer(73, static_cast<int>(d.pattern.size()));
        float total = 0.0f;
        for (float dash : d.pattern) total += std::fabs(dash);
        w.real(40, total);
        for (float dash : d.pattern) w.real(49, dash);
    }
    w.pair(0, "ENDTAB");

    const std::vector<const LotLayers::Layer*> all = layers.all();
    w.pair(0, "TABLE");
    w.pair(2, "LAYER");
    w.integer(70, static_cast<int>(all.size()));
    for (const LotLayers::Layer* l : all) {
        w.pair(0, "LAYER");
        w.pair(2, l->name);
        w.integer(70, l->locked ? 4 : 0);
        // 꺼진 층은 색 번호를 음수로 적는다 (DXF 관례).
        const int aci = aciFromColor(l->color);
        w.integer(62, l->visible ? aci : -aci);
        w.pair(6, lot_linetype::name(l->linetype));
        ++st.layers;
    }
    w.pair(0, "ENDTAB");
    w.pair(0, "ENDSEC");

    // ---- ENTITIES ----
    w.pair(0, "SECTION");
    w.pair(2, "ENTITIES");
    w.out += body.out;
    w.pair(0, "ENDSEC");
    w.pair(0, "EOF");

    LOT_LOG("dxf: wrote " << st.lines << " lines, " << st.circles << " circles, "
            << st.arcs << " arcs, " << st.polylines << " polylines, "
            << st.texts << " texts, " << st.dimensions << " dimensions (exploded), "
            << st.layers << " layers"
            << (st.skipped ? ", skipped " + std::to_string(st.skipped) + " meshes" : ""));
    if (outStats) *outStats = st;
    return w.out;
}

}  // namespace lot_dxf
