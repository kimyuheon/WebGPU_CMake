#include "lot_dimension.h"
#include "line_render_system.h"
#include "lot_camera.h"
#include "text_render_system.h"

#include <cmath>
#include <cstdio>

namespace lot_dim {
namespace {

float length(const vec3& v) { return std::sqrt(dot(v, v)); }

// 방향 벡터를 변환 (이동 성분 없이)
vec3 transformDir(const mat4& m, const vec3& d) {
    return transformPoint(m, d) - transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
}

}  // namespace

std::string formatValue(float value, int precision) {
    if (precision < 0) precision = 0;
    if (precision > 6) precision = 6;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    return buf;
}

Geometry build(const LotGameObject::Dim& dim, const mat4& m, const LotCamera* camera) {
    Geometry g;
    const vec3 p1 = transformPoint(m, dim.p1);
    const vec3 p2 = transformPoint(m, dim.p2);
    const vec3 dl = transformPoint(m, dim.dimLine);
    vec3 n = normalize(transformDir(m, dim.normal));
    if (length(n) < 1e-6f) n = vec3{0.0f, 0.0f, 1.0f};

    // 측정 방향 (평면 안). 두 점이 겹치면 그릴 게 없다.
    const vec3 d = p2 - p1;
    const float len = length(d);
    g.value = len;
    if (len < 1e-6f) return g;
    const vec3 along = d * (1.0f / len);
    // 치수선 오프셋 방향 = 평면 안에서 측정 방향에 수직. dimLine 이 어느 쪽에 있든 그쪽으로.
    vec3 offDir = cross(n, along);
    const float offset = dot(dl - p1, offDir);
    // 치수선 위의 두 끝점: 측정점을 offDir 로 offset 만큼 밀어낸 자리
    const vec3 a = p1 + offDir * offset;
    const vec3 b = p2 + offDir * offset;

    // 보조선: 측정점에서 치수선을 살짝 넘어서까지 (CAD 관례, 화살표 크기만큼)
    const float ext = dim.arrowSize * 0.6f;
    const float sign = (offset >= 0.0f) ? 1.0f : -1.0f;
    g.segments.emplace_back(p1, a + offDir * (ext * sign));
    g.segments.emplace_back(p2, b + offDir * (ext * sign));
    // 치수선
    g.segments.emplace_back(a, b);
    // 화살표: 열린 V. 좁은 치수(화살표 둘이 안 들어감)면 바깥쪽에 둔다.
    const float arrow = dim.arrowSize;
    const bool outside = dim.arrowsOutside || len < arrow * 2.5f;
    const float dir = outside ? -1.0f : 1.0f;  // 안쪽 화살표는 끝에서 안으로 향한다
    const float wing = arrow * 0.35f;
    auto arrowAt = [&](const vec3& tip, float toward) {
        const vec3 back = tip + along * (toward * dir * arrow);
        g.segments.emplace_back(tip, back + offDir * wing);
        g.segments.emplace_back(tip, back - offDir * wing);
    };
    arrowAt(a, 1.0f);
    arrowAt(b, -1.0f);

    // 글자: 치수선 가운데, 오프셋 쪽으로 글자 높이의 반쯤 띄워서. 방향은 치수선을 따라.
    g.text = formatValue(len, dim.precision);
    g.textHeight = dim.textHeight;
    // 글자 틀은 right x up = normal 을 지켜야 거울상이 아니다. 치수선이 측정 방향의
    // 반대편(sign < 0)에 있으면 up 만 뒤집는 게 아니라 180도 돌린다 (둘 다 뒤집기).
    vec3 tRight = along * sign;
    vec3 tUp = offDir * sign;  // 치수선의 '바깥' 쪽이 글자의 위
    if (camera) {
        // 평면 뒷면에서 보면 어떤 글자도 거울상이다 - right 만 뒤집어 화면에서 읽히게 한다
        if (dot(n, camera->getForward()) > 0.0f) tRight = tRight * -1.0f;
        // 화면에서 거꾸로 서지 않게: 오른쪽이 화면 왼쪽을 향하면 180도 (둘 다 뒤집기)
        if (dot(tRight, camera->getRight()) < 0.0f) {
            tRight = tRight * -1.0f;
            tUp = tUp * -1.0f;
        }
    }
    g.textRight = tRight;
    g.textUp = tUp;
    g.textOrigin = (a + b) * 0.5f + tUp * (dim.textHeight * 0.75f);
    return g;
}

void draw(const LotGameObject& obj, const LotCamera& camera, LineRenderSystem& lines,
          TextRenderSystem& text, const vec3& color) {
    if (!obj.isDimension()) return;
    const Geometry g = build(obj.dim, obj.transform.mat4Transform(), &camera);
    for (const auto& s : g.segments) lines.addLine(s.first, s.second, color);
    if (!g.text.empty() && g.textHeight > 0.0f) {
        const vec3 textColor{std::fmin(1.0f, color.x * 1.15f + 0.05f),
                             std::fmin(1.0f, color.y * 1.15f + 0.05f),
                             std::fmin(1.0f, color.z * 1.15f + 0.05f)};
        text.addText(g.text, g.textOrigin, g.textRight, g.textUp, g.textHeight, textColor, 1);
    }
}

std::vector<vec3> outlinePoints(const LotGameObject& obj) {
    std::vector<vec3> pts;
    if (!obj.isDimension()) return pts;
    const Geometry g = build(obj.dim, obj.transform.mat4Transform(), nullptr);
    if (g.segments.size() >= 3) {
        pts.push_back(g.segments[0].first);   // p1
        pts.push_back(g.segments[1].first);   // p2
        pts.push_back(g.segments[2].first);   // 치수선 끝
        pts.push_back(g.segments[2].second);
    }
    return pts;
}

}  // namespace lot_dim

namespace lot_text {
namespace {
TextRenderSystem* g_measurer = nullptr;

vec3 transformDir(const mat4& m, const vec3& d) {
    return transformPoint(m, d) - transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
}
}  // namespace

void setMeasurer(TextRenderSystem* system) { g_measurer = system; }

bool quadCorners(const LotGameObject& obj, vec3 out[4]) {
    if (!obj.isText() || !g_measurer) return false;
    const mat4 m = obj.transform.mat4Transform();
    const vec3 origin = transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
    const vec3 right = normalize(transformDir(m, obj.text.right));
    const vec3 up = normalize(transformDir(m, obj.text.up));
    // 축척은 글자 높이에 반영한다 (right/up 은 단위로 되돌렸으므로)
    const float scale = std::sqrt(dot(transformDir(m, obj.text.up), transformDir(m, obj.text.up)));
    g_measurer->quadCorners(obj.text.content, origin, right, up, obj.text.height * scale,
                            obj.text.hAlign, obj.text.vAlign, out);
    return true;
}

void draw(const LotGameObject& obj, TextRenderSystem& text, const vec3& color) {
    if (!obj.isText() || obj.text.content.empty()) return;
    const mat4 m = obj.transform.mat4Transform();
    const vec3 origin = transformPoint(m, vec3{0.0f, 0.0f, 0.0f});
    const vec3 upScaled = transformDir(m, obj.text.up);
    const float scale = std::sqrt(dot(upScaled, upScaled));
    text.addText(obj.text.content, origin, normalize(transformDir(m, obj.text.right)),
                 normalize(upScaled), obj.text.height * scale, color, obj.text.hAlign, obj.text.vAlign);
}

}  // namespace lot_text
