#include "lot_scene_io.h"
#include "lot_json.h"
#include "lot_log.h"
#include "lot_linetype.h"
#include "lot_material.h"
#include "lot_model.h"
#include "lot_sketch_tool.h"
#include "lot_brep_shape.h"
#include "lot_feature.h"

#include <cmath>
#include <map>

namespace lot_scene {
namespace {

// ---- 좌표계 변환 ----
//
// 웹도 Z-up 으로 옮긴 뒤로는 파일 좌표 = 월드 좌표다. 예전에 +Y 아래 규약이던 때의
// 변환 자리를 남겨두어 (항등), 다시 규약이 갈라지면 여기 한 곳만 고치면 된다.

vec3 toNative(const vec3& v) { return v; }
vec3 fromNative(const vec3& n) { return n; }
quat toNative(const quat& q) { return q; }
quat fromNative(const quat& q) { return q; }
vec3 scaleToNative(const vec3& s) { return s; }
vec3 scaleFromNative(const vec3& s) { return s; }

// ---- JSON 도우미 ----

JsonValue j3(const vec3& v) {
    JsonValue a = JsonValue::makeArray();
    a.push(v.x); a.push(v.y); a.push(v.z);
    return a;
}

JsonValue j4(const quat& q) {
    // 네이티브와 같은 순서: w, x, y, z
    JsonValue a = JsonValue::makeArray();
    a.push(q.w); a.push(q.x); a.push(q.y); a.push(q.z);
    return a;
}

vec3 getv3(const JsonValue* v, const vec3& fallback) {
    if (!v || !v->isArray() || v->array.size() < 3) return fallback;
    return vec3{static_cast<float>(v->array[0].numberOr(0.0)),
                static_cast<float>(v->array[1].numberOr(0.0)),
                static_cast<float>(v->array[2].numberOr(0.0))};
}

quat getq(const JsonValue* v) {
    if (!v || !v->isArray() || v->array.size() < 4) return quat::identity();
    return quat{static_cast<float>(v->array[0].numberOr(1.0)),
                static_cast<float>(v->array[1].numberOr(0.0)),
                static_cast<float>(v->array[2].numberOr(0.0)),
                static_cast<float>(v->array[3].numberOr(0.0))};
}

JsonValue transformJson(const TransformComponent& t) {
    JsonValue o = JsonValue::makeObject();
    o.set("t", j3(toNative(t.translation)));
    o.set("r", j4(toNative(t.rotation)));
    o.set("s", j3(scaleToNative(t.scale)));
    return o;
}

TransformComponent transformFromJson(const JsonValue* o) {
    TransformComponent t;
    if (!o || !o->isObject()) return t;
    t.translation = fromNative(getv3(o->find("t"), vec3{0.0f, 0.0f, 0.0f}));
    t.rotation = fromNative(getq(o->find("r")));
    t.scale = scaleFromNative(getv3(o->find("s"), vec3{1.0f, 1.0f, 1.0f}));
    return t;
}

// 솔리드 형상 - 네이티브 writeBRep / readBRep 과 같은 키 (box / cylinder / extrude + cuts + bosses)
JsonValue pointsJson(const std::vector<vec3>& pts) {
    JsonValue a = JsonValue::makeArray();
    for (const vec3& p : pts) a.push(j3(p));
    return a;
}

std::vector<vec3> pointsFrom(const JsonValue* a) {
    std::vector<vec3> out;
    if (a && a->isArray()) for (const JsonValue& p : a->array) out.push_back(getv3(&p, vec3{0.0f, 0.0f, 0.0f}));
    return out;
}

double numberAt(const JsonValue& o, const char* key, double fallback) {
    const JsonValue* v = o.find(key);
    return v ? v->numberOr(fallback) : fallback;
}

JsonValue brepJson(const lot::LotBRepShape& shape) {
    using K = lot::LotBRepShape::FeatureKind;
    const auto& f = shape.feature();
    JsonValue r = JsonValue::makeObject();
    if (f.kind == K::Box) {
        r.set("type", "box");
        r.set("dimensions", j3(f.dimensions));
        r.set("origin", j3(f.origin));
    } else if (f.kind == K::Cylinder) {
        r.set("type", "cylinder");
        r.set("radius", f.radius);
        r.set("height", f.height);
        r.set("origin", j3(f.origin));
    } else {
        r.set("type", "extrude");
        r.set("height", f.height);
        r.set("direction", j3(f.direction));
        r.set("profile", pointsJson(f.profile));
        if (!f.cuts.empty()) {
            JsonValue cuts = JsonValue::makeArray();
            for (const auto& c : f.cuts) {
                JsonValue jc = JsonValue::makeObject();
                jc.set("depth", c.depth);
                jc.set("profile", pointsJson(c.profile));
                cuts.push(jc);
            }
            r.set("cuts", cuts);
        }
        if (!f.bosses.empty()) {
            JsonValue bosses = JsonValue::makeArray();
            for (const auto& b : f.bosses) {
                JsonValue jb = JsonValue::makeObject();
                jb.set("height", b.height);
                jb.set("profile", pointsJson(b.profile));
                bosses.push(jb);
            }
            r.set("bosses", bosses);
        }
    }
    return r;
}

std::shared_ptr<const lot::LotBRepShape> brepFrom(const JsonValue& v) {
    using lot::LotBRepShape;
    if (!v.isObject()) return {};
    const std::string type = v.find("type") ? v.find("type")->stringOr("") : "";
    if (type == "box") {
        return LotBRepShape::makeBox(getv3(v.find("dimensions"), vec3{0.0f, 0.0f, 0.0f}),
                                     getv3(v.find("origin"), vec3{0.0f, 0.0f, 0.0f}));
    }
    if (type == "cylinder") {
        return LotBRepShape::makeCylinder(static_cast<float>(numberAt(v, "radius", 0.0)),
                                          static_cast<float>(numberAt(v, "height", 0.0)),
                                          getv3(v.find("origin"), vec3{0.0f, 0.0f, 0.0f}));
    }
    if (type != "extrude") return {};
    std::vector<LotBRepShape::CutData> cuts;
    if (const JsonValue* jc = v.find("cuts"); jc && jc->isArray()) {
        for (const JsonValue& c : jc->array) {
            LotBRepShape::CutData cut;
            cut.depth = static_cast<float>(numberAt(c, "depth", 0.0));
            cut.profile = pointsFrom(c.find("profile"));
            cuts.push_back(std::move(cut));
        }
    }
    std::vector<LotBRepShape::BossData> bosses;
    if (const JsonValue* jb = v.find("bosses"); jb && jb->isArray()) {
        for (const JsonValue& b : jb->array) {
            LotBRepShape::BossData boss;
            boss.height = static_cast<float>(numberAt(b, "height", 0.0));
            boss.profile = pointsFrom(b.find("profile"));
            bosses.push_back(std::move(boss));
        }
    }
    return LotBRepShape::makeCutExtrude(pointsFrom(v.find("profile")), getv3(v.find("direction"), vec3{0.0f, 0.0f, 0.0f}),
                                        static_cast<float>(numberAt(v, "height", 0.0)), cuts, bosses);
}

// 열 우선 16 개 (네이티브 featureLinks 의 inv / ci / bi)
JsonValue matJson(const mat4& m) {
    JsonValue a = JsonValue::makeArray();
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) a.push(m.m[c][r]);
    return a;
}

mat4 matFrom(const JsonValue* a) {
    mat4 m = mat4::identity();
    if (a && a->isArray() && a->array.size() == 16) {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) m.m[c][r] = static_cast<float>(a->array[c * 4 + r].numberOr(0.0));
    }
    return m;
}

JsonValue objectJson(const LotGameObject& obj) {
    JsonValue jo = JsonValue::makeObject();
    jo.set("name", "");
    jo.set("color", j3(obj.color));
    if (obj.colorByLayer) jo.set("colorByLayer", true);  // 웹 확장 (네이티브는 무시)
    jo.set("layer", static_cast<double>(obj.layer));
    // 네이티브와 같은 규약: -1 = ByLayer, 그 외는 선종류 id
    jo.set("linetype", obj.linetype == lot_linetype::kByLayer
                           ? -1.0 : static_cast<double>(obj.linetype));
    jo.set("transform", transformJson(obj.transform));

    if (obj.isLight()) {
        // 점 광원. 네이티브에는 아직 없는 키라 그쪽에서는 조용히 무시된다
        // (우리 리더도 모르는 kind 는 세고 건너뛴다 - 한쪽만 알아도 파일은 열린다).
        const auto& l = obj.light;
        JsonValue jl = JsonValue::makeObject();
        jl.set("color", j3(l.color));
        jl.set("intensity", l.intensity);
        jl.set("markerSize", l.markerSize);
        jl.set("orbit", l.orbit);
        jo.set("kind", "light");
        jo.set("light", jl);
        return jo;
    }

    if (obj.isText()) {
        // 네이티브 TextData 와 같은 키. origin 은 로컬 0 (위치는 transform 이 든다).
        const auto& t = obj.text;
        JsonValue jt = JsonValue::makeObject();
        jt.set("content", t.content);
        jt.set("height", t.height);
        jt.set("font", "sans-serif");
        jt.set("origin", j3(vec3{0.0f, 0.0f, 0.0f}));
        jt.set("right", j3(toNative(t.right)));
        jt.set("up", j3(toNative(t.up)));
        jt.set("hAlign", t.hAlign);
        jt.set("vAlign", t.vAlign);
        jt.set("widthFactor", 1.0f);
        jt.set("oblique", 0.0f);
        jo.set("kind", "text");
        jo.set("text", jt);
        return jo;
    }

    if (obj.isDimension()) {
        // 네이티브 DimensionData 의 부분집합. type 0 = Aligned. 나머지 필드는 그쪽 기본값.
        const auto& d = obj.dim;
        JsonValue jd = JsonValue::makeObject();
        jd.set("type", 0);
        jd.set("p1", j3(toNative(d.p1)));
        jd.set("p2", j3(toNative(d.p2)));
        jd.set("p3", j3(vec3{0.0f, 0.0f, 0.0f}));
        jd.set("linearAxis", 0);
        jd.set("dimLine", j3(toNative(d.dimLine)));
        jd.set("normal", j3(toNative(d.normal)));
        jd.set("textHeight", d.textHeight);
        jd.set("arrowSize", d.arrowSize);
        jd.set("precision", d.precision);
        jd.set("arrowsOutside", d.arrowsOutside);
        jo.set("kind", "dimension");
        jo.set("dim", jd);
        return jo;
    }

    if (obj.isHatch()) {
        // 네이티브 scene_lot_io 와 같은 키. 평면은 객체 로컬 (위치 · 회전은 transform).
        const lot_hatch::HatchData& h = *obj.hatch;
        auto j2 = [](const lot_hatch::P2& p) {
            JsonValue a = JsonValue::makeArray();
            a.push(p.x); a.push(p.y);
            return a;
        };
        JsonValue loops = JsonValue::makeArray();
        for (const auto& lp : h.loops) {
            JsonValue l = JsonValue::makeArray();
            for (const auto& p : lp) l.push(j2(p));
            loops.push(l);
        }
        JsonValue lines = JsonValue::makeArray();
        for (const auto& L : h.lines) {
            JsonValue jl = JsonValue::makeObject();
            jl.set("angle", L.angleDeg);
            jl.set("base", j2(L.base));
            jl.set("offset", j2(L.offset));
            JsonValue d = JsonValue::makeArray();
            for (float e : L.dashes) d.push(e);
            jl.set("dashes", d);
            lines.push(jl);
        }
        JsonValue jh = JsonValue::makeObject();
        jh.set("origin", j3(toNative(h.origin)));
        jh.set("right", j3(toNative(h.right)));
        jh.set("up", j3(toNative(h.up)));
        jh.set("loops", loops);
        jh.set("solid", h.solid);
        jh.set("pattern", h.patternName);
        jh.set("angle", h.angleDeg);
        jh.set("scale", h.scale);
        jh.set("lines", lines);
        jo.set("kind", "hatch");
        jo.set("hatch", jh);
        return jo;
    }

    if (obj.isSketch()) {
        // 원/호는 정의로 저장한다 (점 목록은 파생물). 네이티브 CircleData/ArcData 와 같은 키.
        if (obj.hasCurve()) {
            const auto& c = obj.curve;
            JsonValue jc = JsonValue::makeObject();
            jc.set("center", j3(toNative(c.center)));
            jc.set("radius", c.radius);
            jc.set("right", j3(toNative(c.right)));
            jc.set("up", j3(toNative(c.up)));
            if (c.kind == LotGameObject::Curve::Kind::Circle) {
                jo.set("kind", "circle");
                jo.set("circle", jc);
            } else {
                jc.set("start", c.start);
                jc.set("end", c.end);
                jo.set("kind", "arc");
                jo.set("arc", jc);
            }
            return jo;
        }
        // 열린 2점 스케치는 네이티브의 "line", 나머지는 "polyline".
        if (obj.points.size() == 2 && !obj.closed) {
            jo.set("kind", "line");
            JsonValue line = JsonValue::makeObject();
            line.set("a", j3(toNative(obj.points[0])));
            line.set("b", j3(toNative(obj.points[1])));
            jo.set("line", line);
        } else {
            jo.set("kind", "polyline");
            JsonValue pl = JsonValue::makeObject();
            pl.set("closed", obj.closed);
            JsonValue verts = JsonValue::makeArray();
            for (const vec3& p : obj.points) verts.push(j3(toNative(p)));
            pl.set("verts", verts);
            jo.set("polyline", pl);
        }
        return jo;
    }

    // 메시: 정점을 펼쳐 넣는다 (p/n/uv 는 평탄 배열, idx 는 인덱스).
    jo.set("kind", "mesh");
    JsonValue P = JsonValue::makeArray(), N = JsonValue::makeArray(), UV = JsonValue::makeArray();
    JsonValue C = JsonValue::makeArray();
    bool anyColor = false;  // 전부 흰색이면 "c" 를 아예 쓰지 않는다 (파일이 작게)
    for (const Vertex& v : obj.model->getVertices()) {
        const vec3 p = toNative(vec3{v.position[0], v.position[1], v.position[2]});
        const vec3 n = toNative(vec3{v.normal[0], v.normal[1], v.normal[2]});
        P.push(p.x); P.push(p.y); P.push(p.z);
        N.push(n.x); N.push(n.y); N.push(n.z);
        UV.push(v.uv[0]); UV.push(v.uv[1]);
        C.push(v.color[0]); C.push(v.color[1]); C.push(v.color[2]);
        if (v.color[0] != 1.0f || v.color[1] != 1.0f || v.color[2] != 1.0f) anyColor = true;
    }
    JsonValue idx = JsonValue::makeArray();
    for (uint32_t i : obj.model->getIndices()) idx.push(i);
    JsonValue mesh = JsonValue::makeObject();
    mesh.set("p", P);
    mesh.set("n", N);
    mesh.set("uv", UV);
    mesh.set("idx", idx);
    // 웹 확장: 정점 색 (rgb 평탄 배열). 네이티브 포맷에는 없다 - 그쪽은 모르는 키를 무시하고,
    // 여기서는 있으면 읽어 큐브/토러스의 색이 왕복 뒤에도 남는다.
    if (anyColor) mesh.set("c", C);
    jo.set("mesh", mesh);
    // 솔리드는 해석 형상도 - 읽을 때는 이것으로 메시를 다시 만든다 (삼각형은 옛 판독기용 예비)
    if (obj.brep) jo.set("brep", brepJson(*obj.brep));
    return jo;
}

// 스케치 오브젝트를 만들어 넣는다. 점은 이미 웹 로컬 좌표.
void addSketch(LotGameObject::Map& objects, std::vector<vec3> points, bool closed,
               const TransformComponent& t, const vec3& color, uint32_t layer, uint32_t linetype,
               bool byLayer, const LotGameObject::Curve* curve = nullptr) {
    auto obj = LotGameObject::createGameObject();
    obj.transform = t;
    obj.color = color;
    obj.layer = layer;
    obj.linetype = linetype;
    obj.colorByLayer = byLayer;
    obj.points = std::move(points);
    obj.closed = closed;
    if (curve) obj.curve = *curve;
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
}

// "circle" / "arc" 정의를 읽어 웹 좌표로. 값이 없으면 네이티브 기본값 (right X, up Y).
LotGameObject::Curve curveFromJson(const JsonValue& jc, bool arc) {
    LotGameObject::Curve c;
    c.kind = arc ? LotGameObject::Curve::Kind::Arc : LotGameObject::Curve::Kind::Circle;
    c.center = fromNative(getv3(jc.find("center"), vec3{0.0f, 0.0f, 0.0f}));
    c.radius = static_cast<float>(jc.find("radius") ? jc.find("radius")->numberOr(1.0) : 1.0);
    c.right = normalize(fromNative(getv3(jc.find("right"), vec3{1.0f, 0.0f, 0.0f})));
    c.up = normalize(fromNative(getv3(jc.find("up"), vec3{0.0f, 1.0f, 0.0f})));
    if (arc) {
        c.start = static_cast<float>(jc.find("start") ? jc.find("start")->numberOr(0.0) : 0.0);
        c.end = static_cast<float>(jc.find("end") ? jc.find("end")->numberOr(3.14159265) : 3.14159265);
    } else {
        c.start = 0.0f;
        c.end = 6.28318530718f;
    }
    return c;
}

bool loadMesh(const JsonValue& jm, lot_web_device& device, const TransformComponent& t,
              const vec3& color, uint32_t layer, std::shared_ptr<LotMaterial> material,
              LotGameObject::Map& objects) {
    const JsonValue* jp = jm.find("p");
    if (!jp || !jp->isArray() || jp->array.size() < 9) return false;
    const std::vector<float> P = jp->numbers();
    const std::vector<float> N = jm.find("n") ? jm.find("n")->numbers() : std::vector<float>{};
    const std::vector<float> UV = jm.find("uv") ? jm.find("uv")->numbers() : std::vector<float>{};
    const std::vector<float> I = jm.find("idx") ? jm.find("idx")->numbers() : std::vector<float>{};
    const std::vector<float> C = jm.find("c") ? jm.find("c")->numbers() : std::vector<float>{};

    const size_t count = P.size() / 3;
    LotModel::Builder builder;
    builder.vertices.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const vec3 p = fromNative(vec3{P[i * 3], P[i * 3 + 1], P[i * 3 + 2]});
        vec3 n{0.0f, 0.0f, 1.0f};
        if (N.size() >= (i + 1) * 3) n = fromNative(vec3{N[i * 3], N[i * 3 + 1], N[i * 3 + 2]});
        float u = 0.0f, v = 0.0f;
        if (UV.size() >= (i + 1) * 2) { u = UV[i * 2]; v = UV[i * 2 + 1]; }
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (C.size() >= (i + 1) * 3) { r = C[i * 3]; g = C[i * 3 + 1]; b = C[i * 3 + 2]; }
        builder.vertices.push_back(Vertex::make(p.x, p.y, p.z, r, g, b, n.x, n.y, n.z, u, v));
    }
    builder.indices.reserve(I.size());
    for (float f : I) {
        const uint32_t idx = static_cast<uint32_t>(f);
        if (idx >= count) return false;  // 깨진 파일
        builder.indices.push_back(idx);
    }

    auto model = std::make_shared<LotModel>(device, builder);
    if (!model->isReady()) return false;

    auto obj = LotGameObject::createGameObject();
    obj.transform = t;
    obj.color = color;
    obj.layer = layer;
    obj.model = std::move(model);
    obj.material = std::move(material);
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
    return true;
}

}  // namespace

std::string save(const LotGameObject::Map& objects, const LotLayers& layers) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", "lot");
    root.set("version", 4);   // 4 = 솔리드 (brep + featureLinks)
    root.set("generator", "3dengine_web");

    // 층. 네이티브와 같은 키 (color 는 [r, g, b], linetype 은 아직 0 고정).
    JsonValue jlayers = JsonValue::makeArray();
    for (const LotLayers::Layer* l : layers.all()) {
        JsonValue jl = JsonValue::makeObject();
        jl.set("id", static_cast<double>(l->id));
        jl.set("name", l->name);
        jl.set("visible", l->visible);
        jl.set("locked", l->locked);
        jl.set("color", j3(l->color));
        jl.set("linetype", static_cast<double>(l->linetype));
        jl.set("opacity", l->opacity);
        jlayers.push(jl);
    }
    root.set("layers", jlayers);
    JsonValue arr = JsonValue::makeArray();
    int count = 0;
    std::map<LotGameObject::id_t, int> idToIdx;   // 피처 기록은 id 대신 저장 배열 번호 (네이티브와 같다)
    for (const auto& entry : objects) {
        const LotGameObject& obj = entry.second;
        if (!obj.isSketch() && !obj.model && !obj.isDimension() && !obj.isText()
            && !obj.isLight()) continue;  // 뷰어 같은 빈 오브젝트
        idToIdx[entry.first] = count;
        arr.push(objectJson(obj));
        ++count;
    }
    root.set("objects", arr);
    // 피처 기록 - 네이티브 키 그대로 (s 솔리드, k 단면 스케치, inv, c/ci/lc 컷, b/bi/lb 보스, lp 마지막 단면)
    JsonValue links = JsonValue::makeArray();
    auto idx = [&](unsigned id) {
        const auto f = idToIdx.find(id);
        return f == idToIdx.end() ? -1 : f->second;
    };
    for (const auto& entry : objects) {
        const LotGameObject& obj = entry.second;
        if (!obj.featureLink || !idToIdx.count(entry.first)) continue;
        const FeatureLink& L = *obj.featureLink;
        JsonValue jl = JsonValue::makeObject();
        jl.set("s", idx(entry.first));
        jl.set("k", idx(L.sketch));
        jl.set("inv", matJson(L.linkInv));
        JsonValue c = JsonValue::makeArray(), ci = JsonValue::makeArray(), lc = JsonValue::makeArray();
        for (size_t i = 0; i < L.cutSketches.size(); ++i) {
            c.push(idx(L.cutSketches[i]));
            ci.push(matJson(i < L.cutLinkInv.size() ? L.cutLinkInv[i] : mat4::identity()));
        }
        for (const auto& p : L.lastCutProfiles) lc.push(pointsJson(p));
        jl.set("c", c);
        jl.set("ci", ci);
        jl.set("lp", pointsJson(L.lastProfile));
        jl.set("lc", lc);
        if (!L.bossSketches.empty()) {
            JsonValue b = JsonValue::makeArray(), bi = JsonValue::makeArray(), lb = JsonValue::makeArray();
            for (size_t i = 0; i < L.bossSketches.size(); ++i) {
                b.push(idx(L.bossSketches[i]));
                bi.push(matJson(i < L.bossLinkInv.size() ? L.bossLinkInv[i] : mat4::identity()));
            }
            for (const auto& p : L.lastBossProfiles) lb.push(pointsJson(p));
            jl.set("b", b);
            jl.set("bi", bi);
            jl.set("lb", lb);
        }
        links.push(jl);
    }
    root.set("featureLinks", links);
    LOT_LOG("scene: saved " << count << " objects");
    return dumpJson(root, 1);
}

LoadStats load(const std::string& text, lot_web_device& device,
               std::shared_ptr<LotMaterial> defaultMaterial, LotGameObject::Map& objects,
               LotLayers& layers) {
    LoadStats stats;
    JsonValue root;
    if (!parseJson(text, root, stats.error)) {
        stats.error = "scene: not valid JSON - " + stats.error;
        return stats;
    }
    if (root.find("format") == nullptr || root.find("format")->stringOr("") != "lot") {
        stats.error = "scene: not a .lot file (format field missing)";
        return stats;
    }
    const JsonValue* arr = root.find("objects");
    if (!arr || !arr->isArray()) {
        stats.error = "scene: no objects array";
        return stats;
    }

    // 층 먼저. 파일 id 는 우리 id 와 다를 수 있으므로 표를 만들어 오브젝트에 적용한다.
    // 파일의 id 0 은 우리 기본층("0")에 붙인다 - 네이티브도 0 을 기본층으로 쓴다.
    layers.clear();
    std::map<uint32_t, uint32_t> layerMap;
    layerMap.emplace(0u, LotLayers::kDefault);
    if (const JsonValue* jl = root.find("layers"); jl && jl->isArray()) {
        for (const JsonValue& entry : jl->array) {
            if (!entry.isObject()) continue;
            const uint32_t fileId = static_cast<uint32_t>(entry.find("id") ? entry.find("id")->numberOr(0.0) : 0.0);
            const std::string name = entry.find("name") ? entry.find("name")->stringOr("Layer") : "Layer";
            const vec3 color = getv3(entry.find("color"), vec3{0.8f, 0.8f, 0.85f});
            uint32_t id;
            if (fileId == 0 || name == "0") {
                id = LotLayers::kDefault;  // 기본층은 만들지 않고 속성만 덮어쓴다
            } else if (layerMap.count(fileId)) {
                continue;
            } else {
                id = layers.create(name, color);
            }
            if (LotLayers::Layer* l = layers.find(id)) {
                l->color = color;
                l->linetype = static_cast<uint32_t>(
                    entry.find("linetype") ? entry.find("linetype")->numberOr(0.0) : 0.0);
                if (id != LotLayers::kDefault) {
                    l->visible = entry.find("visible") ? entry.find("visible")->boolOr(true) : true;
                    l->locked = entry.find("locked") ? entry.find("locked")->boolOr(false) : false;
                }
                l->opacity = static_cast<float>(entry.find("opacity") ? entry.find("opacity")->numberOr(1.0) : 1.0);
            }
            layerMap.emplace(fileId, id);
            ++stats.layers;
        }
    }
    auto mappedLayer = [&](const JsonValue& jo) {
        const uint32_t fileId = static_cast<uint32_t>(jo.find("layer") ? jo.find("layer")->numberOr(0.0) : 0.0);
        auto it = layerMap.find(fileId);
        return it == layerMap.end() ? LotLayers::kDefault : it->second;
    };

    std::vector<LotGameObject::id_t> idxToId(arr->array.size(), LotGameObject::kInvalidId);
    size_t objIdx = 0;
    for (const JsonValue& jo : arr->array) {
        const size_t myIdx = objIdx++;
        const LotGameObject::id_t firstNew = LotGameObject::peekNextId();
        // 이 항목이 객체를 하나 만들었으면 그 id 를 저장 번호에 묶는다 (피처 기록 복원용)
        struct Bind {
            std::vector<LotGameObject::id_t>& map; size_t i; LotGameObject::id_t first;
            ~Bind() { if (LotGameObject::peekNextId() > first) map[i] = first; }
        } bind{idxToId, myIdx, firstNew};
        if (!jo.isObject()) continue;
        const std::string kind = jo.find("kind") ? jo.find("kind")->stringOr("mesh") : "mesh";
        const TransformComponent t = transformFromJson(jo.find("transform"));
        const vec3 color = getv3(jo.find("color"), vec3{1.0f, 1.0f, 1.0f});
        const uint32_t layerId = mappedLayer(jo);
        const bool colorByLayer = jo.find("colorByLayer") ? jo.find("colorByLayer")->boolOr(false) : false;
        const double ltRaw = jo.find("linetype") ? jo.find("linetype")->numberOr(-1.0) : -1.0;
        const uint32_t linetypeId = (ltRaw < 0.0) ? lot_linetype::kByLayer
                                                  : static_cast<uint32_t>(ltRaw);

        if (kind == "line" && jo.find("line")) {
            const JsonValue* jl = jo.find("line");
            const vec3 a = fromNative(getv3(jl->find("a"), vec3{0.0f, 0.0f, 0.0f}));
            const vec3 b = fromNative(getv3(jl->find("b"), vec3{0.0f, 0.0f, 0.0f}));
            addSketch(objects, {a, b}, false, t, color, layerId, linetypeId, colorByLayer);
            ++stats.lines;
        } else if (kind == "polyline" && jo.find("polyline")) {
            const JsonValue* jp = jo.find("polyline");
            const JsonValue* verts = jp->find("verts");
            std::vector<vec3> pts;
            if (verts && verts->isArray()) {
                for (const JsonValue& v : verts->array) {
                    pts.push_back(fromNative(getv3(&v, vec3{0.0f, 0.0f, 0.0f})));
                }
            }
            if (pts.size() < 2) { ++stats.skipped; continue; }
            const bool closed = jp->find("closed") ? jp->find("closed")->boolOr(false) : false;
            addSketch(objects, std::move(pts), closed, t, color, layerId, linetypeId, colorByLayer);
            ++stats.polylines;
        } else if (kind == "light") {
            const JsonValue* jl = jo.find("light");
            auto obj = LotGameObject::createGameObject();
            obj.transform = t;
            obj.color = color;
            obj.light.valid = true;
            if (jl) {
                obj.light.color = getv3(jl->find("color"), vec3{1.0f, 1.0f, 1.0f});
                obj.light.intensity =
                    static_cast<float>(jl->find("intensity") ? jl->find("intensity")->numberOr(1.0) : 1.0);
                obj.light.markerSize =
                    static_cast<float>(jl->find("markerSize") ? jl->find("markerSize")->numberOr(0.12) : 0.12);
                obj.light.orbit = jl->find("orbit") ? jl->find("orbit")->boolOr(false) : false;
            }
            objects.emplace(obj.getId(), std::move(obj));
            ++stats.lights;
        } else if (kind == "text" && jo.find("text")) {
            const JsonValue* jt = jo.find("text");
            LotGameObject::Text tx;
            tx.valid = true;
            tx.content = jt->find("content") ? jt->find("content")->stringOr("") : "";
            tx.height = static_cast<float>(jt->find("height") ? jt->find("height")->numberOr(0.5) : 0.5);
            tx.right = normalize(fromNative(getv3(jt->find("right"), vec3{1.0f, 0.0f, 0.0f})));
            tx.up = normalize(fromNative(getv3(jt->find("up"), vec3{0.0f, 0.0f, 1.0f})));
            tx.hAlign = static_cast<int>(jt->find("hAlign") ? jt->find("hAlign")->numberOr(0.0) : 0.0);
            tx.vAlign = static_cast<int>(jt->find("vAlign") ? jt->find("vAlign")->numberOr(0.0) : 0.0);
            // 네이티브는 origin 을 따로 든다 - 로컬 오프셋으로 보고 translation 에 얹는다
            const vec3 origin = fromNative(getv3(jt->find("origin"), vec3{0.0f, 0.0f, 0.0f}));
            if (tx.content.empty()) { ++stats.skipped; continue; }
            auto obj = LotGameObject::createGameObject();
            obj.transform = t;
            obj.transform.translation = obj.transform.translation + transformPoint(t.mat4Transform(), origin)
                                        - t.translation;
            obj.color = color;
            obj.text = tx;
            const auto id = obj.getId();
            objects.emplace(id, std::move(obj));
            ++stats.texts;
        } else if (kind == "dimension" && jo.find("dim")) {
            const JsonValue* jd = jo.find("dim");
            const int type = static_cast<int>(jd->find("type") ? jd->find("type")->numberOr(0.0) : 0.0);
            if (type != 0) {  // Linear/Angular/Radius 는 아직 - 건너뛰고 센다
                ++stats.skipped;
                if (stats.skippedKinds.find("dimension(type)") == std::string::npos) {
                    if (!stats.skippedKinds.empty()) stats.skippedKinds += ", ";
                    stats.skippedKinds += "dimension(type)";
                }
                continue;
            }
            LotGameObject::Dim d;
            d.valid = true;
            d.p1 = fromNative(getv3(jd->find("p1"), vec3{0.0f, 0.0f, 0.0f}));
            d.p2 = fromNative(getv3(jd->find("p2"), vec3{1.0f, 0.0f, 0.0f}));
            d.dimLine = fromNative(getv3(jd->find("dimLine"), vec3{0.0f, 0.0f, 0.0f}));
            d.normal = normalize(fromNative(getv3(jd->find("normal"), vec3{0.0f, 0.0f, 1.0f})));
            d.textHeight = static_cast<float>(jd->find("textHeight") ? jd->find("textHeight")->numberOr(0.3) : 0.3);
            d.arrowSize = static_cast<float>(jd->find("arrowSize") ? jd->find("arrowSize")->numberOr(0.15) : 0.15);
            d.precision = static_cast<int>(jd->find("precision") ? jd->find("precision")->numberOr(2.0) : 2.0);
            d.arrowsOutside = jd->find("arrowsOutside") ? jd->find("arrowsOutside")->boolOr(false) : false;
            auto obj = LotGameObject::createGameObject();
            obj.transform = t;
            obj.color = color;
            obj.layer = layerId;
            obj.linetype = linetypeId;
            obj.colorByLayer = colorByLayer;
            obj.dim = d;
            const auto id = obj.getId();
            objects.emplace(id, std::move(obj));
            ++stats.dimensions;
        } else if ((kind == "circle" && jo.find("circle")) || (kind == "arc" && jo.find("arc"))) {
            const bool arc = kind == "arc";
            const LotGameObject::Curve c = curveFromJson(*jo.find(arc ? "arc" : "circle"), arc);
            std::vector<vec3> pts = tessellateArc(c.center, c.radius, c.right, c.up,
                                                  c.start, c.end, /*includeEnd=*/arc);
            if (pts.size() < 2) { ++stats.skipped; continue; }
            addSketch(objects, std::move(pts), !arc, t, color, layerId, linetypeId, colorByLayer, &c);
            arc ? ++stats.arcs : ++stats.circles;
        } else if (kind == "hatch" && jo.find("hatch")) {
            const JsonValue* jh = jo.find("hatch");
            auto h = std::make_shared<lot_hatch::HatchData>();
            h->origin = fromNative(getv3(jh->find("origin"), vec3{0.0f, 0.0f, 0.0f}));
            h->right = normalize(fromNative(getv3(jh->find("right"), vec3{1.0f, 0.0f, 0.0f})));
            h->up = normalize(fromNative(getv3(jh->find("up"), vec3{0.0f, 1.0f, 0.0f})));
            auto p2 = [](const JsonValue* v) {
                lot_hatch::P2 p;
                if (v && v->isArray() && v->array.size() >= 2) {
                    p.x = static_cast<float>(v->array[0].numberOr(0.0));
                    p.y = static_cast<float>(v->array[1].numberOr(0.0));
                }
                return p;
            };
            if (const JsonValue* jl = jh->find("loops"); jl && jl->isArray()) {
                for (const JsonValue& l : jl->array) {
                    std::vector<lot_hatch::P2> lp;
                    for (const JsonValue& q : l.array) if (q.isArray() && q.array.size() >= 2) lp.push_back(p2(&q));
                    if (lp.size() >= 3) h->loops.push_back(std::move(lp));
                }
            }
            if (h->loops.empty()) { ++stats.skipped; continue; }
            h->solid = jh->find("solid") ? jh->find("solid")->boolOr(false) : false;
            h->patternName = jh->find("pattern") ? jh->find("pattern")->stringOr("ANSI31") : "ANSI31";
            h->angleDeg = static_cast<float>(jh->find("angle") ? jh->find("angle")->numberOr(0.0) : 0.0);
            h->scale = static_cast<float>(jh->find("scale") ? jh->find("scale")->numberOr(1.0) : 1.0);
            if (const JsonValue* jl = jh->find("lines"); jl && jl->isArray()) {
                for (const JsonValue& l : jl->array) {
                    lot_hatch::PatternLine L;
                    L.angleDeg = static_cast<float>(l.find("angle") ? l.find("angle")->numberOr(0.0) : 0.0);
                    L.base = p2(l.find("base"));
                    if (l.find("offset")) L.offset = p2(l.find("offset"));
                    if (const JsonValue* d = l.find("dashes"); d && d->isArray()) L.dashes = d->numbers();
                    h->lines.push_back(std::move(L));
                }
            }
            auto obj = LotGameObject::createGameObject();
            obj.transform = t;
            obj.color = color;
            obj.layer = layerId;
            obj.linetype = linetypeId;
            obj.colorByLayer = colorByLayer;
            lot_hatch::attach(obj, std::move(h));
            objects.emplace(obj.getId(), std::move(obj));
            ++stats.hatches;
        } else if (const auto shape = jo.find("brep") ? brepFrom(*jo.find("brep")) : nullptr) {
            // 솔리드 - 저장된 삼각형 대신 해석 형상에서 메시를 다시 만든다 (네이티브와 같다)
            auto model = lot_feature::buildSolidModel(device, *shape);
            if (!model) { ++stats.skipped; continue; }
            auto obj = LotGameObject::createGameObject();
            obj.transform = t;
            obj.color = color;
            obj.layer = layerId;
            obj.colorByLayer = colorByLayer;
            obj.model = std::move(model);
            obj.brep = shape;
            objects.emplace(obj.getId(), std::move(obj));
            ++stats.meshes;
            ++stats.solids;
        } else if (jo.find("mesh")) {
            if (loadMesh(*jo.find("mesh"), device, t, color, layerId, defaultMaterial, objects)) ++stats.meshes;
            else ++stats.skipped;
        } else {
            ++stats.skipped;
            if (stats.skippedKinds.find(kind) == std::string::npos) {
                if (!stats.skippedKinds.empty()) stats.skippedKinds += ", ";
                stats.skippedKinds += kind;
            }
        }
    }

    // 피처 기록 복원 - 저장 번호 -> 새 id
    if (const JsonValue* links = root.find("featureLinks"); links && links->isArray()) {
        auto idOf = [&](const JsonValue* v) -> unsigned {
            const int i = v ? static_cast<int>(v->numberOr(-1.0)) : -1;
            return (i >= 0 && i < static_cast<int>(idxToId.size())) ? idxToId[static_cast<size_t>(i)] : FeatureLink::kNone;
        };
        for (const JsonValue& jl : links->array) {
            if (!jl.isObject()) continue;
            LotGameObject* solid = LotGameObject::find(objects, idOf(jl.find("s")));
            if (!solid || !solid->brep) continue;
            FeatureLink L;
            L.sketch = idOf(jl.find("k"));
            L.linkInv = matFrom(jl.find("inv"));
            const JsonValue* c = jl.find("c");
            const JsonValue* ci = jl.find("ci");
            if (c && c->isArray()) {
                for (size_t i = 0; i < c->array.size(); ++i) {
                    L.cutSketches.push_back(idOf(&c->array[i]));
                    L.cutLinkInv.push_back(ci && ci->isArray() && i < ci->array.size() ? matFrom(&ci->array[i]) : mat4::identity());
                }
            }
            L.lastProfile = pointsFrom(jl.find("lp"));
            if (const JsonValue* lc = jl.find("lc"); lc && lc->isArray()) for (const JsonValue& p : lc->array) L.lastCutProfiles.push_back(pointsFrom(&p));
            const JsonValue* b = jl.find("b");
            const JsonValue* bi = jl.find("bi");
            if (b && b->isArray()) {
                for (size_t i = 0; i < b->array.size(); ++i) {
                    L.bossSketches.push_back(idOf(&b->array[i]));
                    L.bossLinkInv.push_back(bi && bi->isArray() && i < bi->array.size() ? matFrom(&bi->array[i]) : mat4::identity());
                }
            }
            if (const JsonValue* lb = jl.find("lb"); lb && lb->isArray()) for (const JsonValue& p : lb->array) L.lastBossProfiles.push_back(pointsFrom(&p));
            solid->featureLink = std::make_shared<const FeatureLink>(std::move(L));
            ++stats.featureLinks;
        }
    }

    LOT_LOG("scene: loaded " << stats.meshes << " meshes, " << stats.lines << " lines, "
            << stats.polylines << " polylines, " << stats.circles << " circles, "
            << stats.arcs << " arcs, " << stats.dimensions << " dimensions, " << stats.texts
            << " texts, " << stats.lights << " lights, " << stats.layers << " layers, " << stats.hatches << " hatches, " << stats.solids << " solids, "
            << stats.featureLinks << " feature links"
            << (stats.skipped ? " (skipped " + std::to_string(stats.skipped) + ": "
                                + stats.skippedKinds + ")" : ""));
    return stats;
}

}  // namespace lot_scene
