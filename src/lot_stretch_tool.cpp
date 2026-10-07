#include "lot_stretch_tool.h"
#include "line_render_system.h"
#include "lot_cursor_snap.h"
#include "lot_log.h"
#include "lot_mouse_input.h"
#include "lot_picking.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {

constexpr float kTwoPi = 6.28318530718f;
const vec3 kWindowColor{0.4f, 1.0f, 0.5f};   // 걸침 창 (AutoCAD crossing 처럼 초록)
const vec3 kGhostColor{0.4f, 0.8f, 1.0f};
const vec3 kTrackColor{1.0f, 1.0f, 0.6f};

float wrap2pi(float a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0f ? a + kTwoPi : a;
}
float len3(const vec3& v) { return std::sqrt(dot(v, v)); }

vec3 worldDir(const LotGameObject& o, const vec3& local) {
    return transformPoint(o.transform.mat4Transform(), local) - o.transform.translation;
}

// 호의 시작 / 가운데 / 끝 (월드)
void arcPoints(const LotGameObject& o, vec3& s, vec3& m, vec3& e) {
    const mat4 M = o.transform.mat4Transform();
    const auto& c = o.curve;
    auto at = [&](float a) { return transformPoint(M, c.center + (c.right * std::cos(a) + c.up * std::sin(a)) * c.radius); };
    s = at(c.start);
    m = at((c.start + c.end) * 0.5f);
    e = at(c.end);
}

// 세 점을 지나는 원 (평면 위). 거의 한 줄이면 false.
bool circleThrough(const vec3& a, const vec3& b, const vec3& c, const vec3& n, vec3& center) {
    const vec3 ab = b - a, ac = c - a;
    const vec3 abXac = cross(ab, ac);
    const float d = 2.0f * dot(abXac, abXac);
    if (d < 1e-12f * dot(ab, ab) * dot(ac, ac)) return false;
    center = a + (cross(abXac, ab) * dot(ac, ac) + cross(ac, abXac) * dot(ab, ab)) * (1.0f / d);
    (void)n;
    return true;
}

}  // namespace

void StretchTool::start(const LotCamera& camera) {
    plane_ = SketchPlane::fromCamera(camera);
    state_ = State::Corner1;
    grips_.clear();
    number_.clear();
    LOT_LOG("stretch: click the first corner of the crossing window (Esc cancels)");
}

void StretchTool::cancel() {
    if (!isActive()) return;
    LOT_LOG("stretch: cancelled");
    state_ = State::Idle;
    grips_.clear();
    number_.clear();
}

bool StretchTool::cursorPoint(const Context& ctx, vec3& out, bool corrected) const {
    if (ctx.snap.valid()) {
        out = ctx.snap.point;
        return true;
    }
    const lot_pick::Ray ray = lot_pick::screenToRay(ctx.camera, ctx.mouse.x(), ctx.mouse.y(), ctx.width, ctx.height);
    if (!plane_.intersect(ray, out)) return false;
    if (corrected) out = lot_cursor::apply(out, plane_, &base_);   // 직교 / 극좌표 / 그리드
    return true;
}

void StretchTool::collect(const Context& ctx) {
    grips_.clear();
    const float x0 = std::fmin(c1x_, c2x_), x1 = std::fmax(c1x_, c2x_);
    const float y0 = std::fmin(c1y_, c2y_), y1 = std::fmax(c1y_, c2y_);
    auto inside = [&](const vec3& p) {
        float sx, sy;
        if (!ctx.camera.projectToScreen(p, ctx.width, ctx.height, sx, sy)) return false;
        return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
    };
    for (const auto& entry : ctx.objects) {
        const LotGameObject& o = entry.second;
        const auto id = entry.first;
        if (!lot_pick::isSelectable(o)) continue;
        if (o.isSketch() && o.hasCurve()) {
            if (o.curve.kind == LotGameObject::Curve::Kind::Circle) {
                if (inside(transformPoint(o.transform.mat4Transform(), o.curve.center))) grips_.push_back({id, Grip::Kind::Whole});
                continue;
            }
            vec3 s, m, e;
            arcPoints(o, s, m, e);
            const bool is = inside(s), im = inside(m), ie = inside(e);
            if (is && im && ie) { grips_.push_back({id, Grip::Kind::Whole}); continue; }
            if (!is && !ie) continue;   // 끝점이 하나도 안 들면 그대로 (AutoCAD 와 같다)
            if (is) grips_.push_back({id, Grip::Kind::ArcStart});
            if (im) grips_.push_back({id, Grip::Kind::ArcMid});
            if (ie) grips_.push_back({id, Grip::Kind::ArcEnd});
        } else if (o.isSketch()) {
            const std::vector<vec3> pts = o.worldPoints();
            std::vector<size_t> in;
            for (size_t i = 0; i < pts.size(); ++i) if (inside(pts[i])) in.push_back(i);
            if (in.empty()) continue;
            if (in.size() == pts.size()) { grips_.push_back({id, Grip::Kind::Whole}); continue; }
            for (size_t i : in) grips_.push_back({id, Grip::Kind::Vertex, i});
        } else if (o.isDimension()) {
            const mat4 M = o.transform.mat4Transform();
            const bool a = inside(transformPoint(M, o.dim.p1)), b = inside(transformPoint(M, o.dim.p2));
            const bool c = inside(transformPoint(M, o.dim.dimLine));
            if (a && b && c) { grips_.push_back({id, Grip::Kind::Whole}); continue; }
            if (a) grips_.push_back({id, Grip::Kind::DimP1});
            if (b) grips_.push_back({id, Grip::Kind::DimP2});
            if (c) grips_.push_back({id, Grip::Kind::DimLine});
        } else if (inside(o.transform.translation)) {   // 문자 · 광원 · 메시: 기준점
            grips_.push_back({id, Grip::Kind::Whole});
        }
    }
}

void StretchTool::applyTo(LotGameObject::Map& objects, const vec3& d, bool commitNow, EditHistory* history,
                          std::vector<std::pair<vec3, vec3>>* preview) const {
    std::map<LotGameObject::id_t, std::vector<const Grip*>> byId;
    for (const Grip& g : grips_) byId[g.id].push_back(&g);
    std::set<LotGameObject::id_t> ids;
    for (const auto& kv : byId) ids.insert(kv.first);
    EditHistory::Edit edit;
    if (commitNow) {
        edit.label = "stretch";
        edit.before = EditHistory::snapshot(objects, ids);
    }
    auto line = [&](const vec3& a, const vec3& b) { if (preview) preview->emplace_back(a, b); };
    for (const auto& kv : byId) {
        LotGameObject* o = LotGameObject::find(objects, kv.first);
        if (!o) continue;
        const std::vector<const Grip*>& gs = kv.second;
        if (gs.size() == 1 && gs[0]->kind == Grip::Kind::Whole) {
            if (preview) {
                if (o->isSketch()) {
                    const std::vector<vec3> pts = o->worldPoints();
                    const size_t n = pts.size();
                    for (size_t i = 0; i + 1 < n + (o->closed ? 1 : 0); ++i) line(pts[i] + d, pts[(i + 1) % n] + d);
                } else {
                    line(o->transform.translation, o->transform.translation + d);
                }
            }
            if (commitNow) o->transform.translation = o->transform.translation + d;
            continue;
        }
        if (o->isSketch() && !o->hasCurve()) {
            std::vector<vec3> pts = o->worldPoints();
            for (const Grip* g : gs) if (g->index < pts.size()) pts[g->index] = pts[g->index] + d;
            const size_t n = pts.size();
            for (size_t i = 0; i + 1 < n + (o->closed ? 1 : 0); ++i) line(pts[i], pts[(i + 1) % n]);
            if (commitNow) {
                for (const Grip* g : gs) {
                    if (g->index < o->points.size()) o->points[g->index] = o->transform.worldToLocalPoint(pts[g->index]);
                }
            }
        } else if (o->isSketch()) {
            // 호: 끝점 / 가운데를 옮긴 뒤 세 점을 지나는 호로 다시 맞춘다
            vec3 s, m, e;
            arcPoints(*o, s, m, e);
            for (const Grip* g : gs) {
                if (g->kind == Grip::Kind::ArcStart) s = s + d;
                if (g->kind == Grip::Kind::ArcMid) m = m + d;
                if (g->kind == Grip::Kind::ArcEnd) e = e + d;
            }
            const vec3 rx = normalize(worldDir(*o, o->curve.right));
            const vec3 ry = normalize(worldDir(*o, o->curve.up));
            const vec3 n = normalize(cross(rx, ry));
            vec3 C;
            if (!circleThrough(s, m, e, n, C)) continue;   // 한 줄이 되면 그대로 둔다
            const float r = len3(s - C);
            auto ang = [&](const vec3& p) { const vec3 v = p - C; return std::atan2(dot(v, ry), dot(v, rx)); };
            const float as = ang(s), am = ang(m), ae = ang(e);
            float b0, b1;
            if (wrap2pi(am - as) <= wrap2pi(ae - as)) { b0 = as; b1 = as + wrap2pi(ae - as); }   // s -> m -> e 반시계
            else { b0 = ae; b1 = ae + wrap2pi(as - ae); }
            const std::vector<vec3> pts = tessellateArc(C, r, rx, ry, b0, b1, true);
            for (size_t i = 0; i + 1 < pts.size(); ++i) line(pts[i], pts[i + 1]);
            if (commitNow) {
                // 월드 축으로 다시 쓴다 (변환은 이동만)
                o->transform = TransformComponent{};
                o->transform.translation = C;
                o->curve.center = vec3{0.0f, 0.0f, 0.0f};
                o->curve.right = rx;
                o->curve.up = ry;
                o->curve.radius = r;
                o->curve.start = b0;
                o->curve.end = b1;
                o->points = tessellateArc(vec3{0.0f, 0.0f, 0.0f}, r, rx, ry, b0, b1, true);
            }
        } else if (o->isDimension()) {
            const mat4 M = o->transform.mat4Transform();
            for (const Grip* g : gs) {
                vec3* p = g->kind == Grip::Kind::DimP1 ? &o->dim.p1 : g->kind == Grip::Kind::DimP2 ? &o->dim.p2 : &o->dim.dimLine;
                const vec3 w = transformPoint(M, *p);
                line(w, w + d);
                if (commitNow) *p = o->transform.worldToLocalPoint(w + d);
            }
        }
    }
    if (commitNow && history) {
        edit.after = EditHistory::snapshot(objects, ids);
        history->record(std::move(edit));
    }
}

bool StretchTool::commit(const Context& ctx, const vec3& d, EditHistory& history) {
    applyTo(ctx.objects, d, true, &history, nullptr);
    LOT_LOG("stretch: " << grips_.size() << " grips moved by " << len3(d));
    state_ = State::Idle;
    grips_.clear();
    number_.clear();
    return true;
}

void StretchTool::update(const Context& ctx, EditHistory& history) {
    if (!isActive()) return;
    if (state_ == State::Second) {
        vec3 p;
        if (cursorPoint(ctx, p, true)) cursor_ = p;
    }
    if (!ctx.mouse.consumeLeftPress()) { ctx.mouse.consumeLeftRelease(); return; }
    ctx.mouse.consumeLeftRelease();
    switch (state_) {
    case State::Corner1:
        c1x_ = ctx.mouse.x();
        c1y_ = ctx.mouse.y();
        state_ = State::Corner2;
        LOT_LOG("stretch: click the opposite corner");
        break;
    case State::Corner2:
        c2x_ = ctx.mouse.x();
        c2y_ = ctx.mouse.y();
        collect(ctx);
        if (grips_.empty()) {
            LOT_LOG("stretch: nothing in the window - try again");
            state_ = State::Corner1;
        } else {
            state_ = State::Base;
            LOT_LOG("stretch: " << grips_.size() << " grips - click the base point");
        }
        break;
    case State::Base: {
        vec3 p;
        if (!cursorPoint(ctx, p, false)) return;
        base_ = cursor_ = p;
        state_ = State::Second;
        LOT_LOG("stretch: click the second point, or type @dx,dy / a distance");
        break;
    }
    case State::Second: {
        vec3 p;
        if (!cursorPoint(ctx, p, true)) return;
        commit(ctx, p - base_, history);
        break;
    }
    default:
        break;
    }
}

bool StretchTool::typed(const std::string& text, const Context& ctx, EditHistory& history) {
    if (state_ != State::Second || text.empty()) return false;
    if (text[0] == '@') {
        float dx = 0.0f, dy = 0.0f;
        if (std::sscanf(text.c_str() + 1, "%f,%f", &dx, &dy) != 2) return false;
        return commit(ctx, plane_.right * dx + plane_.up * dy, history);
    }
    char* end = nullptr;
    const float dist = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) return false;
    const vec3 dir = cursor_ - base_;
    if (len3(dir) < 1e-9f) { LOT_LOG("stretch: point the cursor in the direction first"); return true; }
    return commit(ctx, normalize(dir) * dist, history);
}

bool StretchTool::finish(const Context& ctx, EditHistory& history) {
    if (!isActive()) return false;
    if (state_ == State::Second && !number_.empty()) {
        const std::string n = number_;
        number_.clear();
        return typed(n, ctx, history);
    }
    cancel();   // 값 없이 Enter = 그만
    return true;
}

std::string StretchTool::hint() const {
    switch (state_) {
    case State::Corner1: return "stretch: click the first corner of the crossing window  [Esc cancels]";
    case State::Corner2: return "stretch: click the opposite corner  [Esc cancels]";
    case State::Base: return "stretch: click the base point  [Esc cancels]";
    case State::Second:
        return "stretch: click the second point, or @dx,dy / distance + Enter" + (number_.empty() ? std::string() : "   [" + number_ + "]")
               + "  [Esc cancels]" + lot_cursor::statusSuffix();
    default: return "";
    }
}

void StretchTool::drawOverlay(LineRenderSystem& lines, const Context& ctx) const {
    if (state_ == State::Corner2) {
        // 걸침 창: 화면 네 모서리를 작업평면에 내려 그린다
        const float xs[4] = {c1x_, ctx.mouse.x(), ctx.mouse.x(), c1x_};
        const float ys[4] = {c1y_, c1y_, ctx.mouse.y(), ctx.mouse.y()};
        vec3 c[4];
        for (int i = 0; i < 4; ++i) {
            const lot_pick::Ray r = lot_pick::screenToRay(ctx.camera, xs[i], ys[i], ctx.width, ctx.height);
            if (!plane_.intersect(r, c[i])) return;
        }
        for (int i = 0; i < 4; ++i) lines.addLine(c[i], c[(i + 1) % 4], kWindowColor);
        return;
    }
    if (state_ != State::Base && state_ != State::Second) return;
    const vec3 d = state_ == State::Second ? cursor_ - base_ : vec3{0.0f, 0.0f, 0.0f};
    std::vector<std::pair<vec3, vec3>> preview;
    applyTo(ctx.objects, d, false, nullptr, &preview);
    const vec3 eye = ctx.camera.getPosition();
    auto lift = [&](const vec3& p) { return p + (eye - p) * 0.002f; };
    for (const auto& seg : preview) lines.addLine(lift(seg.first), lift(seg.second), kGhostColor);
    if (state_ == State::Second) lines.addLine(base_, cursor_, kTrackColor);
}
