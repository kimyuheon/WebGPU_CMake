#include "lot_scene_io.h"
#include "lot_json.h"
#include "lot_log.h"
#include "lot_material.h"
#include "lot_model.h"
#include "lot_sketch_tool.h"

#include <cmath>

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

JsonValue objectJson(const LotGameObject& obj) {
    JsonValue jo = JsonValue::makeObject();
    jo.set("name", "");
    jo.set("color", j3(obj.color));
    jo.set("transform", transformJson(obj.transform));

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
    return jo;
}

// 스케치 오브젝트를 만들어 넣는다. 점은 이미 웹 로컬 좌표.
void addSketch(LotGameObject::Map& objects, std::vector<vec3> points, bool closed,
               const TransformComponent& t, const vec3& color,
               const LotGameObject::Curve* curve = nullptr) {
    auto obj = LotGameObject::createGameObject();
    obj.transform = t;
    obj.color = color;
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
              const vec3& color, std::shared_ptr<LotMaterial> material,
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
    obj.model = std::move(model);
    obj.material = std::move(material);
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
    return true;
}

}  // namespace

std::string save(const LotGameObject::Map& objects) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", "lot");
    root.set("version", 3);
    root.set("generator", "3dengine_web");
    JsonValue arr = JsonValue::makeArray();
    int count = 0;
    for (const auto& entry : objects) {
        const LotGameObject& obj = entry.second;
        if (!obj.isSketch() && !obj.model && !obj.isDimension()) continue;  // 뷰어 같은 빈 오브젝트
        arr.push(objectJson(obj));
        ++count;
    }
    root.set("objects", arr);
    LOT_LOG("scene: saved " << count << " objects");
    return dumpJson(root, 1);
}

LoadStats load(const std::string& text, lot_web_device& device,
               std::shared_ptr<LotMaterial> defaultMaterial, LotGameObject::Map& objects) {
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

    for (const JsonValue& jo : arr->array) {
        if (!jo.isObject()) continue;
        const std::string kind = jo.find("kind") ? jo.find("kind")->stringOr("mesh") : "mesh";
        const TransformComponent t = transformFromJson(jo.find("transform"));
        const vec3 color = getv3(jo.find("color"), vec3{1.0f, 1.0f, 1.0f});

        if (kind == "line" && jo.find("line")) {
            const JsonValue* jl = jo.find("line");
            const vec3 a = fromNative(getv3(jl->find("a"), vec3{0.0f, 0.0f, 0.0f}));
            const vec3 b = fromNative(getv3(jl->find("b"), vec3{0.0f, 0.0f, 0.0f}));
            addSketch(objects, {a, b}, false, t, color);
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
            addSketch(objects, std::move(pts), closed, t, color);
            ++stats.polylines;
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
            addSketch(objects, std::move(pts), !arc, t, color, &c);
            arc ? ++stats.arcs : ++stats.circles;
        } else if (jo.find("mesh")) {
            if (loadMesh(*jo.find("mesh"), device, t, color, defaultMaterial, objects)) ++stats.meshes;
            else ++stats.skipped;
        } else {
            ++stats.skipped;
            if (stats.skippedKinds.find(kind) == std::string::npos) {
                if (!stats.skippedKinds.empty()) stats.skippedKinds += ", ";
                stats.skippedKinds += kind;
            }
        }
    }

    LOT_LOG("scene: loaded " << stats.meshes << " meshes, " << stats.lines << " lines, "
            << stats.polylines << " polylines, " << stats.circles << " circles, "
            << stats.arcs << " arcs, " << stats.dimensions << " dimensions"
            << (stats.skipped ? " (skipped " + std::to_string(stats.skipped) + ": "
                                + stats.skippedKinds + ")" : ""));
    return stats;
}

}  // namespace lot_scene
