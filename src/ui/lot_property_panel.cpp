#include "lot_property_panel.h"
#include "lot_linetype.h"
#include "lot_log.h"
#include "lot_model.h"
#include "lot_node_tree.h"
#include "lot_ui_command.h"
#include "lot_brep_shape.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace lot_ui {
namespace {

std::string num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

std::string hexColor(const vec3& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  static_cast<int>(c.x * 255.0f + 0.5f),
                  static_cast<int>(c.y * 255.0f + 0.5f),
                  static_cast<int>(c.z * 255.0f + 0.5f));
    return buf;
}

// 종류 이름 (속성 머리줄)
const char* kindName(const LotGameObject& o) {
    if (o.isText()) return "문자";
    if (o.isDimension()) return "치수";
    if (o.isLight()) return "광원";
    if (o.isSolid()) return "솔리드";
    if (o.isHatch()) return "해치";
    if (o.isSketch()) {
        if (o.curve.kind == LotGameObject::Curve::Kind::Circle) return "원";
        if (o.curve.kind == LotGameObject::Curve::Kind::Arc) return "호";
        if (o.points.size() == 2) return "선";
        return "폴리선";
    }
    if (o.model) return "메시";
    return "객체";
}

}  // namespace

std::string LotPropertyPanel::diffJson(const LotLayers& layers, const LotGameObject::Map& objects,
                                       const EditController& edit) {
    // 공통 값: 모두 같으면 그 값, 섞였으면 표시 (-2 / "")
    int count = 0;
    long layer = -3, linetype = -3, byLayer = -3;
    std::string color = "?";
    std::map<std::string, int> kinds;
    const LotGameObject* single = nullptr;
    LotGameObject::id_t singleId = LotGameObject::kInvalidId;
    for (LotGameObject::id_t id : edit.selection()) {
        const LotGameObject* o = LotGameObject::find(objects, id);
        if (!o) continue;
        ++count;
        single = o;
        singleId = id;
        ++kinds[kindName(*o)];
        const long l = static_cast<long>(o->layer);
        layer = (layer == -3 || layer == l) ? l : -2;
        const long lt = (o->linetype == lot_linetype::kByLayer) ? -1 : static_cast<long>(o->linetype);
        linetype = (linetype == -3 || linetype == lt) ? lt : -2;
        const long bl = o->colorByLayer ? 1 : 0;
        byLayer = (byLayer == -3 || byLayer == bl) ? bl : -2;
        const std::string c = hexColor(o->color);
        color = (color == "?" || color == c) ? c : "";
    }

    std::string kindText;
    for (const auto& kv : kinds) {
        if (!kindText.empty()) kindText += ", ";
        kindText += kv.first + (kinds.size() > 1 || count > 1 ? " " + std::to_string(kv.second) : "");
    }

    std::string j = "{\"count\":" + std::to_string(count) + ",\"kind\":" + quote(kindText)
                  + ",\"layer\":" + std::to_string(count ? layer : -3)
                  + ",\"linetype\":" + std::to_string(count ? linetype : -3)
                  + ",\"byLayer\":" + std::to_string(count ? byLayer : -3)
                  + ",\"color\":" + quote(count ? color : "") + ",\"layers\":[";
    bool first = true;
    for (const LotLayers::Layer* l : layers.all()) {
        if (!first) j += ",";
        first = false;
        j += "{\"id\":" + std::to_string(l->id) + ",\"name\":" + quote(l->name) + "}";
    }
    j += "]";

    if (count == 1 && single) {
        const LotGameObject& o = *single;
        const vec3& t = o.transform.translation;
        j += ",\"single\":{\"id\":" + std::to_string(singleId) + ",\"label\":"
           + quote(LotNodeTree::label(o, singleId))
           + ",\"x\":" + num(t.x) + ",\"y\":" + num(t.y) + ",\"z\":" + num(t.z);
        // 종류별 읽기 전용 값 [이름, 값]
        std::string info;
        auto add = [&](const char* name, const std::string& value) {
            if (!info.empty()) info += ",";
            info += "[" + quote(name) + "," + quote(value) + "]";
        };
        if (o.isSketch()) {
            const std::vector<vec3> pts = o.worldPoints();
            double len = 0.0;
            const size_t n = pts.size();
            const size_t segs = o.closed ? n : (n ? n - 1 : 0);
            for (size_t i = 0; i < segs; ++i) {
                const vec3 d = pts[(i + 1) % n] - pts[i];
                len += std::sqrt(static_cast<double>(dot(d, d)));
            }
            if (o.hasCurve()) {
                const float s = std::sqrt(dot(o.transform.scale, o.transform.scale) / 3.0f);
                add("반지름", num(o.curve.radius * s));
                if (o.curve.kind == LotGameObject::Curve::Kind::Arc) {
                    add("시작 각", num(o.curve.start * 57.2957795) + "°");
                    add("끝 각", num(o.curve.end * 57.2957795) + "°");
                }
                add(o.curve.kind == LotGameObject::Curve::Kind::Circle ? "둘레" : "호 길이", num(len));
            } else {
                add(n == 2 ? "길이" : "총 길이", num(len));
                if (n != 2) {
                    add("점", std::to_string(n));
                    add("닫힘", o.closed ? "예" : "아니오");
                }
            }
        } else if (o.isText()) {
            j += ",\"text\":" + quote(o.text.content) + ",\"textHeight\":" + num(o.text.height);
            const char* h[] = {"왼쪽", "가운데", "오른쪽"};
            const char* v[] = {"기준선", "가운데", "위"};
            add("맞춤", std::string(h[std::max(0, std::min(2, o.text.hAlign))]) + " / "
                        + v[std::max(0, std::min(2, o.text.vAlign))]);
        } else if (o.isSolid()) {
            // 해석 형상의 값 (네이티브 속성 창의 B-Rep 칸). 높이는 고칠 수 있다 - 관통 컷은 새 높이까지 관통.
            const auto& f = o.brep->feature();
            if (f.kind == lot::LotBRepShape::FeatureKind::Extrude) j += ",\"solidHeight\":" + num(f.height);
            add("부피", num(o.brep->volume()));
            add("겉넓이", num(o.brep->surfaceArea()));
            add("면 / 모서리", std::to_string(o.brep->faces().size()) + " / " + std::to_string(o.brep->edges().size()));
            if (!f.cuts.empty()) add("컷", std::to_string(f.cuts.size()));
            if (!f.bosses.empty()) add("보스", std::to_string(f.bosses.size()));
            if (o.brep->layered()) add("모양", "층 (불리언)");
            if (o.featureLink && !o.featureLink->error.empty()) add("오류", o.featureLink->error);
        } else if (o.model) {
            add("삼각형", std::to_string(o.model->getTriangleCount()));
            const vec3& s = o.transform.scale;
            add("축척", num(s.x) + ", " + num(s.y) + ", " + num(s.z));
        }
        j += ",\"info\":[" + info + "]}";
    }
    j += "}";

    if (j == last_) return std::string();
    last_ = j;
    return j;
}

bool LotPropertyPanel::edit(const std::string& key, const std::string& value,
                            LotGameObject::Map& objects, EditController& edit) {
    if (edit.selection().size() != 1) return false;
    const LotGameObject::id_t id = *edit.selection().begin();
    LotGameObject* o = LotGameObject::find(objects, id);
    if (!o) return false;

    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    const bool isNumber = end && end != value.c_str();

    EditHistory::Edit e;
    e.label = "property";
    e.before = EditHistory::snapshot(objects, edit.selection());
    if (key == "x" || key == "y" || key == "z") {
        if (!isNumber) return false;
        vec3& t = o->transform.translation;
        (key == "x" ? t.x : key == "y" ? t.y : t.z) = static_cast<float>(v);
    } else if (key == "text" && o->isText()) {
        if (value.empty()) return false;   // 비우려면 지운다 (문자 도구와 같은 규칙)
        o->text.content = value;
    } else if (key == "textHeight" && o->isText()) {
        if (!isNumber || !(v > 0.0)) return false;
        o->text.height = static_cast<float>(v);
    } else {
        return false;
    }
    e.after = EditHistory::snapshot(objects, edit.selection());
    edit.history().record(std::move(e));
    LOT_LOG("property: " << key << " = " << value << " (object " << id << ")");
    return true;
}

}  // namespace lot_ui
