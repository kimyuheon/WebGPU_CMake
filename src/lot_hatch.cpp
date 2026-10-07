#include "lot_hatch.h"
#include "lot_game_object.h"

#include "third_party/mapbox/earcut.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>

namespace lot_hatch {
namespace {

constexpr float kPi = 3.14159265358979323846f;
float cross2(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
float dot2(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
P2 rot(P2 v, float deg) {
    const float r = deg * kPi / 180.0f, c = std::cos(r), s = std::sin(r);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool contains(const std::vector<std::vector<P2>>& loops, const P2& p) {
    bool inside = false;
    for (const auto& loop : loops) {
        const std::size_t n = loop.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const P2& a = loop[i];
            const P2& b = loop[j];
            if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-20f) + a.x)) inside = !inside;
        }
    }
    return inside;
}

float loopArea(const std::vector<P2>& loop) {
    double a = 0.0;
    for (std::size_t i = 0, n = loop.size(); i < n; ++i) {
        const P2& p = loop[i];
        const P2& q = loop[(i + 1) % n];
        a += double(p.x) * q.y - double(q.x) * p.y;
    }
    return static_cast<float>(std::fabs(a) * 0.5);
}

std::vector<std::pair<P2, P2>> generateSegments(const HatchData& h, std::size_t maxSegments) {
    std::vector<std::pair<P2, P2>> out;
    if (h.loops.empty() || h.lines.empty()) return out;
    P2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
    std::size_t edges = 0;
    for (const auto& loop : h.loops) {
        for (const P2& p : loop) {
            mn = {std::fmin(mn.x, p.x), std::fmin(mn.y, p.y)};
            mx = {std::fmax(mx.x, p.x), std::fmax(mx.y, p.y)};
        }
        edges += loop.size();
    }
    if (edges < 3 || mx.x <= mn.x || mx.y <= mn.y) return out;
    const P2 corners[4] = {mn, {mx.x, mn.y}, mx, {mn.x, mx.y}};
    constexpr long kMaxLinesPerFamily = 20000;
    std::vector<float> ts;

    for (const PatternLine& L : h.lines) {
        const P2 d = rot({1.0f, 0.0f}, L.angleDeg), n{-d.y, d.x};
        const float nd = dot2(L.offset, n);
        if (std::fabs(nd) < 1e-9f) continue;   // 오프셋이 선과 평행 - 무한 겹침
        float nmin = 1e30f, nmax = -1e30f;
        for (const P2& c : corners) {
            const float v = dot2(c, n);
            nmin = std::fmin(nmin, v);
            nmax = std::fmax(nmax, v);
        }
        const float nb = dot2(L.base, n);
        float k0 = (nmin - nb) / nd, k1 = (nmax - nb) / nd;
        if (k0 > k1) std::swap(k0, k1);
        const long kmin = static_cast<long>(std::floor(k0)) - 1, kmax = static_cast<long>(std::ceil(k1)) + 1;
        if (kmax - kmin > kMaxLinesPerFamily) continue;   // 축척이 너무 작다 - 그리지 않는다 (화면이 까매진다)
        float period = 0.0f;
        for (float e : L.dashes) period += std::fabs(e);
        const bool dashed = !L.dashes.empty() && period > 1e-9f;

        for (long k = kmin; k <= kmax; ++k) {
            const P2 o = L.base + L.offset * static_cast<float>(k);
            ts.clear();
            for (const auto& loop : h.loops) {
                const std::size_t cnt = loop.size();
                for (std::size_t i = 0; i < cnt; ++i) {
                    const P2 p = loop[i], q = loop[(i + 1) % cnt], e = q - p;
                    const float den = cross2(d, e);
                    if (std::fabs(den) < 1e-12f) continue;
                    const P2 w = p - o;
                    const float s = cross2(w, d) / den;
                    if (s < 0.0f || s >= 1.0f) continue;
                    ts.push_back(cross2(w, e) / den);
                }
            }
            if (ts.size() < 2) continue;
            std::sort(ts.begin(), ts.end());
            for (std::size_t i = 0; i + 1 < ts.size(); i += 2) {
                const float ta = ts[i], tb = ts[i + 1];
                if (tb - ta < 1e-9f) continue;
                if (!dashed) {
                    out.emplace_back(o + d * ta, o + d * tb);
                    if (out.size() >= maxSegments) return out;
                    continue;
                }
                // 대시 - 기준점(t=0)에 위상을 맞춘다
                float t = std::floor(ta / period) * period;
                while (t < tb) {
                    for (float e : L.dashes) {
                        if (e > 0.0f) {
                            const float a = std::fmax(t, ta), b = std::fmin(t + e, tb);
                            if (b > a) {
                                out.emplace_back(o + d * a, o + d * b);
                                if (out.size() >= maxSegments) return out;
                            }
                            t += e;
                        } else if (e < 0.0f) {
                            t += -e;
                        } else {   // 점 - 아주 짧은 선
                            if (t >= ta && t <= tb) {
                                const float dot = std::fmax(period * 0.02f, 1e-4f);
                                out.emplace_back(o + d * t, o + d * (t + dot));
                                if (out.size() >= maxSegments) return out;
                            }
                        }
                        if (t >= tb) break;
                    }
                }
            }
        }
    }
    return out;
}

Fill triangulate(const HatchData& h) {
    Fill fill;
    // 루프 깊이 (다른 루프 안에 몇 겹): 짝수 = 바깥, 홀수 = 구멍
    const std::size_t n = h.loops.size();
    std::vector<int> depth(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (h.loops[i].size() < 3) { depth[i] = -1; continue; }
        const P2 p = h.loops[i][0];
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j || h.loops[j].size() < 3) continue;
            if (contains({h.loops[j]}, p)) ++depth[i];
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (depth[i] < 0 || (depth[i] % 2) != 0) continue;
        using Pt = std::array<float, 2>;
        std::vector<std::vector<Pt>> poly;
        std::vector<P2> flat;
        auto push = [&](const std::vector<P2>& loop) {
            std::vector<Pt> ring;
            ring.reserve(loop.size());
            for (const P2& p : loop) {
                ring.push_back({p.x, p.y});
                flat.push_back(p);
            }
            poly.push_back(std::move(ring));
        };
        push(h.loops[i]);
        for (std::size_t j = 0; j < n; ++j) {
            if (depth[j] != depth[i] + 1) continue;
            if (contains({h.loops[i]}, h.loops[j][0])) push(h.loops[j]);
        }
        const auto idx = mapbox::earcut<uint32_t>(poly);
        const auto base = static_cast<uint32_t>(fill.points.size());
        fill.points.insert(fill.points.end(), flat.begin(), flat.end());
        for (auto k : idx) fill.indices.push_back(base + k);
    }
    return fill;
}

namespace {
// acad.pat 원문(인치): 각도, x, y, dx, dy, 대시... - 25.4 를 곱해 mm 로 (네이티브와 같은 표)
struct PatLine {
    float angle, x, y, dx, dy;
    std::vector<float> dashes;
};
const std::map<std::string, std::vector<PatLine>>& patTable() {
    static const std::map<std::string, std::vector<PatLine>> t = {
        {"ANSI31", {{45, 0, 0, 0, .125f, {}}}},
        {"ANSI32", {{45, 0, 0, 0, .375f, {}}, {45, .176776695f, 0, 0, .375f, {}}}},
        {"ANSI33", {{45, 0, 0, 0, .25f, {}}, {45, .176776695f, 0, 0, .25f, {.125f, -.0625f}}}},
        {"ANSI34", {{45, 0, 0, 0, .75f, {}}, {45, .176776695f, 0, 0, .75f, {}}, {45, .353553391f, 0, 0, .75f, {}}, {45, .530330086f, 0, 0, .75f, {}}}},
        {"ANSI35", {{45, 0, 0, 0, .25f, {}}, {45, .176776695f, 0, 0, .25f, {.3125f, -.0625f, 0, -.0625f}}}},
        {"ANSI36", {{45, 0, 0, .176776695f, .25f, {.3125f, -.0625f, 0, -.0625f}}}},
        {"ANSI37", {{45, 0, 0, 0, .125f, {}}, {135, 0, 0, 0, .125f, {}}}},
        {"ANSI38", {{45, 0, 0, 0, .125f, {.3125f, -.0625f}}, {135, 0, 0, 0, .125f, {}}}},
        {"NET", {{0, 0, 0, 0, .125f, {}}, {90, 0, 0, 0, .125f, {}}}},
        {"LINE", {{0, 0, 0, 0, .125f, {}}}},
        {"DOTS", {{0, 0, 0, .03125f, .0625f, {0, -.0625f}}}},
        {"BRICK", {{0, 0, 0, 0, .25f, {}}, {90, 0, 0, .25f, .5f, {.25f, -.25f}}}},
        {"STEEL", {{45, 0, 0, 0, .125f, {}}, {45, 0, .0625f, 0, .125f, {}}}},
        {"CROSS", {{0, 0, 0, .25f, .25f, {.125f, -.375f}}, {90, .0625f, -.0625f, .25f, .25f, {.125f, -.375f}}}},
    };
    return t;
}
}  // namespace

bool builtinPattern(const std::string& name, std::vector<PatternLine>& out) {
    out.clear();
    auto it = patTable().find(upper(name));
    if (it == patTable().end()) return false;
    for (const PatLine& L : it->second) {
        PatternLine hl;
        hl.angleDeg = L.angle;
        hl.base = P2{L.x, L.y} * 25.4f;
        hl.offset = rot(P2{L.dx, L.dy} * 25.4f, L.angle);   // .pat 의 dx/dy 는 선 방향 기준 -> 평면 좌표로
        for (float e : L.dashes) hl.dashes.push_back(e * 25.4f);
        out.push_back(hl);
    }
    return true;
}

std::vector<PatternLine> transformPattern(const std::vector<PatternLine>& def, float scale, float angleDeg) {
    std::vector<PatternLine> out;
    out.reserve(def.size());
    const float s = scale > 0.0f ? scale : 1.0f;
    for (const PatternLine& L : def) {
        PatternLine hl = L;
        hl.angleDeg = L.angleDeg + angleDeg;
        hl.base = rot(L.base * s, angleDeg);
        hl.offset = rot(L.offset * s, angleDeg);
        for (auto& e : hl.dashes) e *= s;
        out.push_back(hl);
    }
    return out;
}

void attach(LotGameObject& obj, std::shared_ptr<const HatchData> h) {
    auto toLocal = [&](const P2& q) { return h->origin + h->right * q.x + h->up * q.y; };
    obj.closed = true;
    obj.points.clear();
    const std::vector<P2>* outer = nullptr;
    float best = -1.0f;
    for (const auto& lp : h->loops) {
        const float a = loopArea(lp);
        if (a > best) { best = a; outer = &lp; }
    }
    if (outer) for (const P2& q : *outer) obj.points.push_back(toLocal(q));
    obj.hatchSegments.reset();
    if (!h->solid) {
        auto segs = std::make_shared<std::vector<vec3>>();
        for (const auto& s2 : generateSegments(*h)) {
            segs->push_back(toLocal(s2.first));
            segs->push_back(toLocal(s2.second));
        }
        obj.hatchSegments = segs;
    }
    obj.hatch = std::move(h);
}

}  // namespace lot_hatch
