#include "lot_scene_io.h"
#include "lot_json.h"
#include "lot_log.h"
#include "lot_material.h"
#include "lot_model.h"

#include <cmath>

namespace lot_scene {
namespace {

// ---- 좌표계 변환 (헤더 주석 참고) ----

vec3 toNative(const vec3& v) { return vec3{v.x, v.z, -v.y}; }
vec3 fromNative(const vec3& n) { return vec3{n.x, -n.z, n.y}; }

// 웹 -> 네이티브 축 회전 = X 둘레 -90도. 위치는 toNative 가 이 회전을 펼친 것이다.
const quat kAxisRot = quat::angleAxis(-1.57079632679f, vec3{1.0f, 0.0f, 0.0f});

quat toNative(const quat& q) { return normalize(kAxisRot * q * kAxisRot.conjugate()); }
quat fromNative(const quat& q) { return normalize(kAxisRot.conjugate() * q * kAxisRot); }

// 축이 자리를 바꾸므로 (y <-> z) 스케일도 같이 바꾼다. 부호는 스케일에 없다.
vec3 scaleToNative(const vec3& s) { return vec3{s.x, s.z, s.y}; }
vec3 scaleFromNative(const vec3& s) { return vec3{s.x, s.z, s.y}; }

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

    if (obj.isSketch()) {
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
               const TransformComponent& t, const vec3& color) {
    auto obj = LotGameObject::createGameObject();
    obj.transform = t;
    obj.color = color;
    obj.points = std::move(points);
    obj.closed = closed;
    const auto id = obj.getId();
    objects.emplace(id, std::move(obj));
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
        vec3 n{0.0f, -1.0f, 0.0f};
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
        if (!obj.isSketch() && !obj.model) continue;  // 뷰어 같은 빈 오브젝트
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
            << stats.polylines << " polylines"
            << (stats.skipped ? " (skipped " + std::to_string(stats.skipped) + ": "
                                + stats.skippedKinds + ")" : ""));
    return stats;
}

}  // namespace lot_scene
