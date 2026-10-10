// 피처 치수의 화면 쪽 (네이티브 first_app/feature_dims.cpp 의 표시 · 입력): 상태바 "치수" 가 켜져 있고 솔리드 하나를
// 고르면 그 치수가 파랗게 뜬다. 치수를 한 번 누르면 무엇인지, 두 번 누르면 입력창에 값 - Enter 로 고치고 다시 만든다.
// 치수는 장면 객체가 아니라 정의에서 바로 선 · 글자로 그린다 (저장 · 되돌리기 · 선택 대상이 아니다).
#include "app/lot_app.h"

#include "lot_dimension.h"
#include "lot_feature_dims.h"
#include "lot_log.h"
#include "lot_picking.h"

#include <emscripten/emscripten.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const vec3 kDimBlue{0.25f, 0.62f, 1.0f};     // 피처 치수 = 파랑 (보통 치수 · 선택 노랑과 구분)
const vec3 kDimOrange{1.0f, 0.55f, 0.1f};    // 고치는 중인 치수

LotGameObject::id_t g_for = LotGameObject::kInvalidId;   // 지금 치수를 보여 주는 솔리드
std::vector<lot_feature_dims::Dim> g_dims;
std::vector<lot_dim::Geometry> g_geo;
int g_editing = -1;                                       // 입력창으로 고치는 치수 번호
LotGameObject::id_t g_editId = LotGameObject::kInvalidId;
int g_lastHit = -1;
double g_lastClickMs = 0.0;

std::string fmtValue(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

float len3(const vec3& v) { return std::sqrt(dot(v, v)); }

// 피처 치수 하나 -> 웹 정렬 치수 정의. 지름은 원을 가로지르는 정렬 치수 + "Ø".
LotGameObject::Dim toWebDim(const lot_feature_dims::Dim& d, float textH) {
    LotGameObject::Dim w;
    w.valid = true;
    w.normal = d.normal;
    w.textHeight = textH;
    w.arrowSize = textH * 0.6f;
    w.precision = 1;
    if (d.diameter) {
        const float r = len3(d.p2 - d.p1);
        const vec3 dir = r > 1e-9f ? (d.p2 - d.p1) * (1.0f / r) : vec3{1.0f, 0.0f, 0.0f};
        w.p1 = d.p1 - dir * r;
        w.p2 = d.p1 + dir * r;
        w.dimLine = d.p1;          // 치수선이 중심을 지난다
        w.prefix = "\xC3\x98";     // Ø
    } else {
        w.p1 = d.p1;
        w.p2 = d.p2;
        w.dimLine = d.dimLine;
    }
    return w;
}

// 지름 (네이티브 Diameter 치수): 반대편 원 위 -> 중심 -> 원 위 -> 바깥 (지시선), 원 위 두 곳에 화살표,
// 글자는 지시선 바깥 끝에 - 같은 중심 지름 (보스 Ø44 안 Ø24) 은 collect 가 지시선을 90° 씩 돌려 둔다.
lot_dim::Geometry diameterGeometry(const lot_feature_dims::Dim& d, float textH) {
    lot_dim::Geometry g;
    const vec3 c = d.p1;
    const float r = len3(d.p2 - d.p1);
    if (r < 1e-9f) return g;
    const vec3 dir = (d.p2 - d.p1) * (1.0f / r);
    const vec3 n = len3(d.normal) > 1e-9f ? d.normal * (1.0f / len3(d.normal)) : vec3{0.0f, 0.0f, 1.0f};
    const vec3 side = cross(n, dir);
    const vec3 a = c - dir * r, b = c + dir * r, tail = d.dimLine;
    g.segments.emplace_back(a, tail);
    const float arrow = textH * 0.6f, wing = arrow * 0.35f;
    g.segments.emplace_back(b, b - dir * arrow + side * wing);   // 원 위에서 안쪽으로 (바깥 쪽 화살표)
    g.segments.emplace_back(b, b - dir * arrow - side * wing);
    g.segments.emplace_back(a, a + dir * arrow + side * wing);
    g.segments.emplace_back(a, a + dir * arrow - side * wing);
    g.value = 2.0f * r;
    g.text = "\xC3\x98" + lot_dim::formatValue(2.0f * r, 1);
    g.textHeight = textH;
    // 글자: 지시선을 따라, 화면에서 읽히게 (lot_dim::build 와 같은 규칙)
    vec3 tRight = dir, tUp = side;
    const LotCamera& cam = doc().camera;
    if (dot(n, cam.getForward()) > 0.0f) tRight = tRight * -1.0f;
    if (dot(tRight, cam.getRight()) < 0.0f) { tRight = tRight * -1.0f; tUp = tUp * -1.0f; }
    g.textRight = tRight;
    g.textUp = tUp;
    // 바깥 끝에서 글자 폭의 반만큼 더 나가 원과 겹치지 않게
    const float halfW = 0.32f * textH * static_cast<float>(g.text.size());
    g.textOrigin = tail + dir * halfW + tUp * (textH * 0.25f);
    g.segments.emplace_back(tail, tail + dir * (2.0f * halfW));   // 글자 밑줄
    return g;
}

// 커서 아래 치수 번호 (선 6px 안, 글자 상자 안). 없으면 -1.
int hitTest(float mx, float my, float width, float height) {
    const LotCamera& cam = doc().camera;
    int best = -1;
    float bestD = 7.0f;
    for (size_t i = 0; i < g_geo.size(); ++i) {
        const lot_dim::Geometry& g = g_geo[i];
        for (const auto& s : g.segments) {
            float ax, ay, bx, by;
            if (!cam.projectToScreen(s.first, width, height, ax, ay) || !cam.projectToScreen(s.second, width, height, bx, by)) continue;
            const float d = lot_pick::distancePointToSegment2D(mx, my, ax, ay, bx, by);
            if (d < bestD) { bestD = d; best = static_cast<int>(i); }
        }
        float tx, ty;
        if (!g.text.empty() && cam.projectToScreen(g.textOrigin, width, height, tx, ty)) {
            const float wpp = cam.worldPerPixel(g.textOrigin, height);
            const float hpx = wpp > 0.0f ? g.textHeight / wpp : 12.0f;
            const float halfW = 0.32f * hpx * static_cast<float>(g.text.size()) + 3.0f;
            if (std::fabs(mx - tx) <= halfW && my <= ty + 3.0f && my >= ty - hpx - 3.0f) return static_cast<int>(i);
        }
    }
    return best;
}

}  // namespace

void cancelFeatureDimEdit() {
    if (g_editing < 0) return;
    g_editing = -1;
    g_editId = LotGameObject::kInvalidId;
    js_hideTextInput();
}

void updateFeatureDims(float width, float height) {
    // 대상: 치수 표시가 켜져 있고, 도구가 없고, 솔리드 하나를 골랐을 때 (값을 고치는 중이면 그 솔리드)
    LotGameObject::id_t target = LotGameObject::kInvalidId;
    if (g_display.dims && !toolsTakeClicks()) {
        const auto& sel = doc().edit.selection();
        if (sel.size() == 1) {
            const LotGameObject* o = LotGameObject::find(doc().objects, *sel.begin());
            if (o && o->isSolid() && !o->hidden && layerVisible(*o)) target = *sel.begin();
        }
    }
    if (g_editing >= 0 && LotGameObject::find(doc().objects, g_editId)) target = g_editId;
    if (target != g_for) { g_lastHit = -1; if (g_editing >= 0 && target != g_editId) cancelFeatureDimEdit(); }
    g_for = target;
    g_dims.clear();
    g_geo.clear();
    if (target == LotGameObject::kInvalidId) return;

    // 매 프레임 다시 모은다 (몇십 개 - 카메라를 돌리면 앞면 · 글자 방향이 따라온다)
    const LotGameObject& o = *LotGameObject::find(doc().objects, target);
    vec3 toCam = doc().camera.getPosition() - o.transform.translation;
    g_dims = lot_feature_dims::collect(doc().objects, target, toCam);
    vec3 lo, hi;
    float size = 1.0f;
    const std::set<LotGameObject::id_t> only{target};
    if (objectBounds(&only, lo, hi)) size = std::fmax(1e-3f, len3(hi - lo));
    const float th = 0.035f * size;
    for (const auto& d : g_dims) {
        g_geo.push_back(d.diameter ? diameterGeometry(d, th) : lot_dim::build(toWebDim(d, th), mat4::identity(), &doc().camera));
    }

    // 클릭: 치수 위면 가져간다 (선택이 풀리지 않게). 두 번이면 입력창.
    if (!g_mouse.peekLeftPress()) return;
    const int hit = hitTest(g_mouse.x(), g_mouse.y(), width, height);
    if (hit < 0) return;
    g_mouse.consumeLeftPress();
    const bool dblEvent = g_mouse.consumeLeftDoubleClick();   // 브라우저의 dblclick (두 누름이 한 프레임에 와도)
    const double now = emscripten_get_now();
    const bool dbl = hit == g_lastHit && (dblEvent || now - g_lastClickMs < 400.0);
    g_lastClickMs = now;
    g_lastHit = hit;
    const auto& d = g_dims[static_cast<size_t>(hit)];
    if (!dbl) {
        LOT_LOG("feature: dimension " << d.label << " " << fmtValue(d.value) << " - double-click to change");
        return;
    }
    g_lastHit = -1;
    g_editing = hit;
    g_editId = target;
    g_editingTextId = LotGameObject::kInvalidId;
    js_showTextInput((d.label + " - new value, Enter to apply").c_str(), fmtValue(d.value).c_str());
    LOT_LOG("feature: editing dimension " << d.label);
}

void drawFeatureDims(float width, float height) {
    (void)width;
    (void)height;
    for (size_t i = 0; i < g_geo.size(); ++i) {
        const lot_dim::Geometry& g = g_geo[i];
        const vec3 col = static_cast<int>(i) == g_editing ? kDimOrange : kDimBlue;
        for (const auto& s : g.segments) g_lineSystem->addLine(s.first, s.second, col);
        if (!g.text.empty() && g.textHeight > 0.0f) {
            const vec3 tc{std::fmin(1.0f, col.x * 1.15f + 0.05f), std::fmin(1.0f, col.y * 1.15f + 0.05f), std::fmin(1.0f, col.z * 1.15f + 0.05f)};
            g_textSystem->addText(g.text, g.textOrigin, g.textRight, g.textUp, g.textHeight, tc, 1);
        }
    }
}

bool featureDimTextEntered(const std::string& text) {
    if (g_editing < 0) return false;
    const int index = g_editing;
    const LotGameObject::id_t id = g_editId;
    g_editing = -1;
    g_editId = LotGameObject::kInvalidId;
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !(v > 0.0f)) {
        LOT_LOG("feature: dimension - type a positive value");
        return true;
    }
    std::string why;
    if (!lot_feature_dims::set(doc().objects, id, index, v, g_renderer->getDevice(), doc().edit.history(), why)) {
        LOT_LOG("feature: cannot change the dimension - " << why);
    }
    return true;
}

// 지금 보이는 피처 치수 [{label, value, x, y}] (x, y = 글자 화면 자리). 테스트 도구가 더블클릭할 자리를 찾는다.
// JSON 은 malloc - JS 가 free 한다.
extern "C" EMSCRIPTEN_KEEPALIVE
char* lot_featureDimsJson() {
    const auto& sc = g_renderer->getSwapchain();
    const float w = static_cast<float>(sc.getWidth()), h = static_cast<float>(sc.getHeight());
    std::string j = "[";
    for (size_t i = 0; i < g_dims.size() && i < g_geo.size(); ++i) {
        float x = -1.0f, y = -1.0f;
        doc().camera.projectToScreen(g_geo[i].textOrigin, w, h, x, y);
        char buf[96];
        std::snprintf(buf, sizeof(buf), ",\"value\":%.4f,\"x\":%.1f,\"y\":%.1f}", g_dims[i].value, x, y);
        if (i) j += ",";
        j += "{\"label\":" + lot_ui::quote(g_dims[i].label) + buf;
    }
    j += "]";
    char* out = static_cast<char*>(std::malloc(j.size() + 1));
    if (out) std::memcpy(out, j.c_str(), j.size() + 1);
    return out;
}
