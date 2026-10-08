#include "lot_brep_shape.h"

#include <manifold/manifold.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace lot {
    namespace {
        constexpr float kTol = 1e-5f;
        constexpr double kPi = 3.1415926535897932384626433832795;

        bool finiteVec(const glm::vec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool unitVec(const glm::vec3& v) {
            return finiteVec(v) && std::abs(glm::length(v) - 1.0f) <= 1e-4f;
        }

        glm::vec3 perpendicular(const glm::vec3& n) {
            glm::vec3 x = glm::cross(n, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::dot(x, x) < 1e-8f) x = glm::cross(n, glm::vec3(0.0f, 1.0f, 0.0f));
            return glm::normalize(x);
        }

        double profileArea(const std::vector<glm::vec3>& profile, const glm::vec3& normal) {
            if (profile.size() < 3) return 0.0;
            const glm::vec3 u = perpendicular(normal);
            const glm::vec3 v = glm::cross(normal, u);
            double twice = 0.0;
            for (std::size_t i = 0; i < profile.size(); ++i) {
                const auto& a = profile[i];
                const auto& b = profile[(i + 1) % profile.size()];
                twice += static_cast<double>(glm::dot(a, u)) * glm::dot(b, v)
                       - static_cast<double>(glm::dot(b, u)) * glm::dot(a, v);
            }
            return 0.5 * twice;
        }

        double profilePerimeter(const std::vector<glm::vec3>& profile) {
            double result = 0.0;
            for (std::size_t i = 0; i < profile.size(); ++i)
                result += glm::length(profile[(i + 1) % profile.size()] - profile[i]);
            return result;
        }

        std::vector<glm::vec3> cleanProfile(const std::vector<glm::vec3>& input) {
            std::vector<glm::vec3> result;
            result.reserve(input.size());
            for (const auto& p : input) {
                if (!finiteVec(p)) return {};
                if (result.empty() || glm::length(p - result.back()) > kTol) result.push_back(p);
            }
            if (result.size() > 2 && glm::length(result.front() - result.back()) <= kTol)
                result.pop_back();
            return result;
        }

        double cross2(const glm::dvec2& a, const glm::dvec2& b) {
            return a.x * b.y - a.y * b.x;
        }

        bool lineIntersection(const glm::dvec2& p, const glm::dvec2& r,
                              const glm::dvec2& q, const glm::dvec2& s,
                              glm::dvec2& result) {
            const double denom = cross2(r, s);
            const double scale = std::max(1.0, glm::length(r) * glm::length(s));
            if (std::abs(denom) <= 1e-10 * scale) return false;
            const double t = cross2(q - p, s) / denom;
            result = p + r * t;
            return std::isfinite(result.x) && std::isfinite(result.y);
        }

        bool pointOnSegment(const glm::dvec2& p, const glm::dvec2& a,
                            const glm::dvec2& b, double lengthTolerance,
                            double areaTolerance) {
            return std::abs(cross2(b - a, p - a)) <= areaTolerance &&
                   p.x >= std::min(a.x, b.x) - lengthTolerance && p.x <= std::max(a.x, b.x) + lengthTolerance &&
                   p.y >= std::min(a.y, b.y) - lengthTolerance && p.y <= std::max(a.y, b.y) + lengthTolerance;
        }

        int orientation2(const glm::dvec2& a, const glm::dvec2& b,
                         const glm::dvec2& c, double tolerance) {
            const double value = cross2(b - a, c - a);
            return value > tolerance ? 1 : value < -tolerance ? -1 : 0;
        }

        bool segmentsIntersect(const glm::dvec2& a, const glm::dvec2& b,
                               const glm::dvec2& c, const glm::dvec2& d,
                               double lengthTolerance, double areaTolerance) {
            const int abC = orientation2(a, b, c, areaTolerance);
            const int abD = orientation2(a, b, d, areaTolerance);
            const int cdA = orientation2(c, d, a, areaTolerance);
            const int cdB = orientation2(c, d, b, areaTolerance);
            if (abC * abD < 0 && cdA * cdB < 0) return true;
            return (abC == 0 && pointOnSegment(c, a, b, lengthTolerance, areaTolerance)) ||
                   (abD == 0 && pointOnSegment(d, a, b, lengthTolerance, areaTolerance)) ||
                   (cdA == 0 && pointOnSegment(a, c, d, lengthTolerance, areaTolerance)) ||
                   (cdB == 0 && pointOnSegment(b, c, d, lengthTolerance, areaTolerance));
        }

        bool validSimpleProfile2D(const std::vector<glm::dvec2>& points) {
            if (points.size() < 3 || points.size() > kMaxBRepProfilePoints) return false;
            glm::dvec2 minimum = points.front(), maximum = points.front();
            for (const auto& p : points) {
                minimum = glm::min(minimum, p);
                maximum = glm::max(maximum, p);
            }
            const double scale = std::max(1.0, glm::length(maximum - minimum));
            const double lengthTolerance = 1e-7 * scale;
            const double areaTolerance = 1e-10 * scale * scale;
            double twiceArea = 0.0;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto& a = points[i];
                const auto& b = points[(i + 1) % points.size()];
                if (glm::length(b - a) <= lengthTolerance) return false;
                twiceArea += cross2(a, b);
            }
            // makeExtrude가 순서를 뒤집으면 안정적인 side face ID가 깨진다.
            if (twiceArea <= areaTolerance) return false;

            for (std::size_t i = 0; i < points.size(); ++i) {
                const std::size_t iNext = (i + 1) % points.size();
                for (std::size_t j = i + 1; j < points.size(); ++j) {
                    const std::size_t jNext = (j + 1) % points.size();
                    if (i == j || iNext == j || jNext == i) continue;
                    if (segmentsIntersect(points[i], points[iNext], points[j], points[jNext],
                                          lengthTolerance, areaTolerance)) return false;
                }
            }
            return true;
        }

        bool pointInsideProfile2D(const glm::dvec2& point,
                                  const std::vector<glm::dvec2>& profile,
                                  double lengthTolerance,
                                  double areaTolerance) {
            bool inside = false;
            for (std::size_t i = 0, j = profile.size() - 1; i < profile.size(); j = i++) {
                const auto& a = profile[j];
                const auto& b = profile[i];
                if (pointOnSegment(point, a, b, lengthTolerance, areaTolerance)) return false;
                const bool crosses = (a.y > point.y) != (b.y > point.y);
                if (crosses) {
                    const double x = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
                    if (x > point.x) inside = !inside;
                }
            }
            return inside;
        }

        bool disjointProfiles2D(const std::vector<glm::dvec2>& a,
                                const std::vector<glm::dvec2>& b,
                                double lengthTolerance,
                                double areaTolerance) {
            for (std::size_t i = 0; i < a.size(); ++i) {
                for (std::size_t j = 0; j < b.size(); ++j) {
                    if (segmentsIntersect(a[i], a[(i + 1) % a.size()],
                                          b[j], b[(j + 1) % b.size()],
                                          lengthTolerance, areaTolerance)) return false;
                }
            }
            return !pointInsideProfile2D(a.front(), b, lengthTolerance, areaTolerance) &&
                   !pointInsideProfile2D(b.front(), a, lengthTolerance, areaTolerance);
        }

        std::vector<glm::dvec2> projectProfile2D(const std::vector<glm::vec3>& profile,
                                                 const glm::vec3& u,
                                                 const glm::vec3& v) {
            std::vector<glm::dvec2> result;
            result.reserve(profile.size());
            for (const auto& point : profile)
                result.emplace_back(glm::dot(point, u), glm::dot(point, v));
            return result;
        }

    }

    // 팩터리 전용 조립기. LotBRepShape의 공개 API는 읽기 전용으로 유지하면서
    // 생성 시에만 위상 배열을 채운다.
    struct LotBRepShapeBuilder {
            LotBRepShape& shape;
            std::map<std::pair<BRepId, BRepId>, BRepId> edgeByVertices;

            BRepId lineEdge(BRepId a, BRepId b, bool& reversed) {
                const auto key = std::minmax(a, b);
                if (const auto found = edgeByVertices.find(key); found != edgeByVertices.end()) {
                    const auto& edge = shape.edges_[found->second];
                    reversed = edge.firstVertex != a;
                    return found->second;
                }
                BRepEdge edge;
                edge.id = static_cast<BRepId>(shape.edges_.size());
                edge.kind = BRepCurveKind::Line;
                edge.firstVertex = a;
                edge.lastVertex = b;
                shape.edges_.push_back(edge);
                edgeByVertices.emplace(key, edge.id);
                reversed = false;
                return edge.id;
            }

            BRepId loop(const std::vector<BRepId>& vertexIds) {
                BRepLoop loop;
                loop.id = static_cast<BRepId>(shape.loops_.size());
                loop.coedges.reserve(vertexIds.size());
                for (std::size_t i = 0; i < vertexIds.size(); ++i) {
                    bool reversed = false;
                    const BRepId edge = lineEdge(vertexIds[i], vertexIds[(i + 1) % vertexIds.size()], reversed);
                    loop.coedges.push_back({edge, reversed});
                }
                shape.loops_.push_back(loop);
                return loop.id;
            }

            void appendLoop(BRepId faceId, const std::vector<BRepId>& vertexIds) {
                shape.faces_[faceId].loops.push_back(loop(vertexIds));
            }

            // 고리 여러 개(첫째 = 바깥)와 곡면을 직접 받아 면 하나 — 층 모양용(세 점으로 법선을 셈하면 한 줄 위 점에서 틀린다)
            BRepId faceWith(const std::vector<std::vector<BRepId>>& loops, const BRepSurface& surface) {
                BRepFace f;
                f.id = static_cast<BRepId>(shape.faces_.size());
                f.surface = surface;
                for (const auto& l : loops) f.loops.push_back(loop(l));
                shape.faces_.push_back(f);
                return f.id;
            }

            static std::shared_ptr<const LotBRepShape> buildLayered(
                const std::vector<glm::vec3>& profile, const glm::vec3& direction, float height,
                const std::vector<LotBRepShape::CutData>& cuts, const std::vector<LotBRepShape::BossData>& bosses);

            BRepId face(const std::vector<BRepId>& vertexIds) {
                const BRepId loopId = loop(vertexIds);

                const glm::vec3& p0 = shape.vertices_[vertexIds[0]].point;
                const glm::vec3& p1 = shape.vertices_[vertexIds[1]].point;
                const glm::vec3& p2 = shape.vertices_[vertexIds[2]].point;
                const glm::vec3 x = glm::normalize(p1 - p0);
                const glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p1));

                BRepFace face;
                face.id = static_cast<BRepId>(shape.faces_.size());
                face.surface.kind = BRepSurfaceKind::Plane;
                face.surface.origin = p0;
                face.surface.normal = n;
                face.surface.xDirection = x;
                face.loops.push_back(loopId);
                shape.faces_.push_back(face);
                return face.id;
            }
    };

    // 층 모양 — 바닥 돌출 ∪ 보스 − 컷 을 Manifold 로 계산하고(기둥은 모두 같은 방향), 결과 삼각형을 이웃·같은 평면끼리
    // 묶어 면으로, 묶음 둘레 모서리를 고리로 되짚는다. Manifold 결과는 물샐틈없는 다양체라 두 면이 나누는 모서리는
    // 양쪽 고리에 같은 정점 열로 나온다(T 이음 없음). 컷 기둥은 단순 위상과 같은 규칙(겹치는 위/아래 보스 끝까지).
    std::shared_ptr<const LotBRepShape> LotBRepShapeBuilder::buildLayered(
        const std::vector<glm::vec3>& profile, const glm::vec3& direction, float height,
        const std::vector<LotBRepShape::CutData>& cuts, const std::vector<LotBRepShape::BossData>& bosses) {
        using manifold::Manifold;
        const glm::vec3 u = perpendicular(direction);
        const glm::vec3 v = glm::cross(direction, u);
        const double plane = glm::dot(profile.front(), direction);
        auto poly = [&](const std::vector<glm::vec3>& pts) {
            manifold::SimplePolygon sp;
            sp.reserve(pts.size());
            for (const auto& p : pts) sp.push_back(manifold::vec2(glm::dot(p, u), glm::dot(p, v)));
            return sp;
        };
        double scale = 1.0;
        for (const auto& p : profile) scale = std::max(scale, static_cast<double>(glm::length(p - profile.front())));
        scale = std::max(scale, static_cast<double>(height));
        const double eps = 1e-4 * scale;   // 컷 기둥을 면 너머로 살짝 — 같은 평면끼리 빼기에서 막이 남지 않게
        auto prism = [&](const std::vector<glm::vec3>& pts, double z0, double z1) {
            return Manifold::Extrude(manifold::Polygons{poly(pts)}, z1 - z0).Translate(manifold::vec3(0.0, 0.0, z0));
        };
        std::vector<std::vector<glm::dvec2>> bosses2;
        for (const auto& b : bosses) bosses2.push_back(projectProfile2D(b.profile, u, v));
        Manifold solid = prism(profile, 0.0, height);
        for (const auto& b : bosses)
            solid = b.height > 0.0f ? solid + prism(b.profile, height, height + b.height)
                                    : solid + prism(b.profile, b.height, 0.0);
        for (const auto& c : cuts) {
            const auto c2 = projectProfile2D(c.profile, u, v);
            double top = height, bottom = 0.0;
            for (std::size_t k = 0; k < bosses.size(); ++k) {
                if (disjointProfiles2D(c2, bosses2[k], 1e-7 * scale, 1e-10 * scale * scale)) continue;
                if (bosses[k].height > 0.0f) top = std::max(top, static_cast<double>(height + bosses[k].height));
                else bottom = std::min(bottom, static_cast<double>(bosses[k].height));
            }
            const double lower = top - c.depth;
            solid = solid - prism(c.profile, lower <= bottom + 1e-6 * scale ? bottom - eps : lower, top + eps);
        }
        if (solid.Status() != manifold::Manifold::Error::NoError || solid.IsEmpty()) return {};
        if (solid.Decompose().size() != 1) return {};   // 덩어리가 둘 이상 — 한 솔리드가 아니다

        const manifold::MeshGL64 mesh = solid.GetMeshGL64();
        const std::size_t np = static_cast<std::size_t>(mesh.numProp);
        const std::size_t nv = mesh.vertProperties.size() / np, nt = mesh.triVerts.size() / 3;
        if (nt < 4) return {};
        std::vector<glm::dvec3> P(nv);
        for (std::size_t i = 0; i < nv; ++i)
            P[i] = {mesh.vertProperties[i * np], mesh.vertProperties[i * np + 1], mesh.vertProperties[i * np + 2]};
        auto tv = [&](std::size_t t, int k) { return static_cast<std::size_t>(mesh.triVerts[t * 3 + static_cast<std::size_t>(k)]); };
        std::vector<glm::dvec3> N(nt);
        std::vector<double> A(nt);
        for (std::size_t t = 0; t < nt; ++t) {
            const glm::dvec3 c = glm::cross(P[tv(t, 1)] - P[tv(t, 0)], P[tv(t, 2)] - P[tv(t, 0)]);
            A[t] = glm::length(c);
            N[t] = A[t] > 0.0 ? c / A[t] : glm::dvec3(0.0);
        }
        // 모서리 → 두 삼각형
        auto key = [](std::size_t a, std::size_t b) { return (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b); };
        std::unordered_map<std::uint64_t, std::vector<std::size_t>> edgeTris;
        edgeTris.reserve(nt * 3);
        for (std::size_t t = 0; t < nt; ++t)
            for (int k = 0; k < 3; ++k) edgeTris[key(tv(t, k), tv(t, (k + 1) % 3))].push_back(t);
        std::vector<std::size_t> parent(nt);
        std::iota(parent.begin(), parent.end(), 0);
        std::function<std::size_t(std::size_t)> find = [&](std::size_t x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
        const double tinyArea = 1e-12 * scale * scale;
        for (const auto& [k, ts] : edgeTris) {
            if (ts.size() != 2) return {};   // 다양체가 아니다
            const std::size_t a = ts[0], b = ts[1];
            if (A[a] <= tinyArea || A[b] <= tinyArea) continue;
            if (glm::dot(N[a], N[b]) > 1.0 - 1e-7) parent[find(a)] = find(b);
        }
        for (std::size_t t = 0; t < nt; ++t) {   // 바늘 삼각형은 이웃 면에 붙인다
            if (A[t] > tinyArea) continue;
            for (int k = 0; k < 3; ++k) {
                const auto& ts = edgeTris[key(tv(t, k), tv(t, (k + 1) % 3))];
                const std::size_t o = ts[0] == t ? ts[1] : ts[0];
                if (A[o] > tinyArea) { parent[find(t)] = find(o); break; }
            }
        }
        std::map<std::size_t, std::vector<std::size_t>> groups;
        for (std::size_t t = 0; t < nt; ++t) groups[find(t)].push_back(t);

        auto shape = std::shared_ptr<LotBRepShape>(new LotBRepShape());
        LotBRepShapeBuilder builder{*shape};
        std::vector<BRepId> vid(nv, kInvalidBRepId);
        auto toWorld = [&](const glm::dvec3& p) {
            return u * static_cast<float>(p.x) + v * static_cast<float>(p.y) + direction * static_cast<float>(plane + p.z);
        };
        auto vertexOf = [&](std::size_t i) {
            if (vid[i] == kInvalidBRepId) {
                vid[i] = static_cast<BRepId>(shape->vertices_.size());
                shape->vertices_.push_back({vid[i], toWorld(P[i])});
            }
            return vid[i];
        };
        for (const auto& [root, tris] : groups) {
            // 둘레 = 이웃이 다른 묶음인 방향 모서리
            std::unordered_map<std::size_t, std::vector<std::size_t>> next;
            glm::dvec3 n(0.0);
            for (std::size_t t : tris) {
                n += N[t] * A[t];
                for (int k = 0; k < 3; ++k) {
                    const std::size_t a = tv(t, k), b = tv(t, (k + 1) % 3);
                    const auto& ts = edgeTris[key(a, b)];
                    const std::size_t o = ts[0] == t ? ts[1] : ts[0];
                    if (find(o) != root) next[a].push_back(b);
                }
            }
            if (glm::length(n) <= 0.0) return {};
            n = glm::normalize(n);
            std::vector<std::vector<std::size_t>> rings;
            for (auto& [start, outs] : next) {
                while (!outs.empty()) {
                    std::vector<std::size_t> ring;
                    std::size_t cur = start;
                    for (std::size_t guard = 0; guard <= nt * 3; ++guard) {
                        auto& o = next[cur];
                        if (o.empty()) return {};   // 고리가 안 닫힌다
                        const std::size_t nx = o.back();
                        o.pop_back();
                        ring.push_back(cur);
                        cur = nx;
                        if (cur == start) break;
                    }
                    if (cur != start || ring.size() < 3) return {};
                    rings.push_back(std::move(ring));
                }
            }
            if (rings.empty()) return {};
            // 바깥 고리 = 법선 방향으로 넓이가 가장 큰(양수) 것
            std::size_t outerIdx = 0;
            double best = -1e300;
            for (std::size_t r = 0; r < rings.size(); ++r) {
                glm::dvec3 acc(0.0);
                for (std::size_t i = 0; i < rings[r].size(); ++i)
                    acc += glm::cross(P[rings[r][i]], P[rings[r][(i + 1) % rings[r].size()]]);
                const double a = glm::dot(acc, n);
                if (a > best) { best = a; outerIdx = r; }
            }
            std::swap(rings[0], rings[outerIdx]);
            std::vector<std::vector<BRepId>> loops;
            for (const auto& ring : rings) {
                auto& l = loops.emplace_back();
                for (std::size_t i : ring) l.push_back(vertexOf(i));
            }
            BRepSurface surface;
            surface.kind = BRepSurfaceKind::Plane;
            surface.normal = glm::normalize(u * static_cast<float>(n.x) + v * static_cast<float>(n.y) + direction * static_cast<float>(n.z));
            surface.origin = shape->vertices_[loops[0][0]].point;
            glm::vec3 x = shape->vertices_[loops[0][1]].point - surface.origin;
            x -= surface.normal * glm::dot(x, surface.normal);
            surface.xDirection = glm::length(x) > 1e-9f ? glm::normalize(x) : perpendicular(surface.normal);
            builder.faceWith(loops, surface);
        }

        shape->feature_.kind = LotBRepShape::FeatureKind::Extrude;
        shape->feature_.height = height;
        shape->feature_.direction = direction;
        shape->feature_.profile = profile;
        shape->feature_.cuts = cuts;
        shape->feature_.bosses = bosses;
        shape->layered_ = true;
        shape->layeredVolume_ = solid.Volume();
        shape->layeredArea_ = solid.SurfaceArea();
        if (!shape->validateClosed()) return {};
        return shape;
    }

    namespace {
        void setError(std::string* error, const std::string& value) {
            if (error) *error = value;
        }
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::makeBox(
        const glm::vec3& dimensions, const glm::vec3& center) {
        if (!finiteVec(dimensions) || !finiteVec(center) ||
            dimensions.x <= kTol || dimensions.y <= kTol || dimensions.z <= kTol)
            return {};

        auto shape = std::shared_ptr<LotBRepShape>(new LotBRepShape());
        shape->feature_.kind = FeatureKind::Box;
        shape->feature_.origin = center;
        shape->feature_.dimensions = dimensions;

        const glm::vec3 h = dimensions * 0.5f;
        const glm::vec3 points[] = {
            center + glm::vec3{-h.x, -h.y, -h.z}, center + glm::vec3{ h.x, -h.y, -h.z},
            center + glm::vec3{ h.x,  h.y, -h.z}, center + glm::vec3{-h.x,  h.y, -h.z},
            center + glm::vec3{-h.x, -h.y,  h.z}, center + glm::vec3{ h.x, -h.y,  h.z},
            center + glm::vec3{ h.x,  h.y,  h.z}, center + glm::vec3{-h.x,  h.y,  h.z}
        };
        for (const auto& point : points)
            shape->vertices_.push_back({static_cast<BRepId>(shape->vertices_.size()), point});

        LotBRepShapeBuilder builder{*shape};
        builder.face({0, 3, 2, 1}); // -Z
        builder.face({4, 5, 6, 7}); // +Z
        builder.face({0, 1, 5, 4}); // -Y
        builder.face({1, 2, 6, 5}); // +X
        builder.face({2, 3, 7, 6}); // +Y
        builder.face({3, 0, 4, 7}); // -X
        if (!shape->validateClosed()) return {};
        return shape;
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::makeCylinder(
        float radius, float height, const glm::vec3& center) {
        if (!std::isfinite(radius) || !std::isfinite(height) || !finiteVec(center) ||
            radius <= kTol || height <= kTol)
            return {};

        auto shape = std::shared_ptr<LotBRepShape>(new LotBRepShape());
        shape->feature_.kind = FeatureKind::Cylinder;
        shape->feature_.origin = center;
        shape->feature_.radius = radius;
        shape->feature_.height = height;
        shape->feature_.direction = {0.0f, 0.0f, 1.0f};

        for (float z : {-height * 0.5f, height * 0.5f}) {
            BRepEdge edge;
            edge.id = static_cast<BRepId>(shape->edges_.size());
            edge.kind = BRepCurveKind::Circle;
            edge.center = center + glm::vec3(0.0f, 0.0f, z);
            edge.normal = {0.0f, 0.0f, 1.0f};
            edge.xDirection = {1.0f, 0.0f, 0.0f};
            edge.radius = radius;
            shape->edges_.push_back(edge);
        }

        // 캡 두 개와 원통 옆면은 같은 해석 원 모서리를 공유한다.
        shape->loops_ = {
            {0, {{0, true}}},  {1, {{1, false}}},
            {2, {{0, false}}}, {3, {{1, true}}}
        };
        BRepFace bottom;
        bottom.id = 0;
        bottom.surface = {BRepSurfaceKind::Plane, center + glm::vec3(0, 0, -height * 0.5f), {0, 0, -1}, {1, 0, 0}, 0};
        bottom.loops = {0};
        BRepFace top;
        top.id = 1;
        top.surface = {BRepSurfaceKind::Plane, center + glm::vec3(0, 0, height * 0.5f), {0, 0, 1}, {1, 0, 0}, 0};
        top.loops = {1};
        BRepFace side;
        side.id = 2;
        side.surface = {BRepSurfaceKind::Cylinder, center, {0, 0, 1}, {1, 0, 0}, radius};
        side.loops = {2, 3};
        shape->faces_ = {bottom, top, side};

        if (!shape->validateClosed()) return {};
        return shape;
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::pushPullFace(
        BRepId faceId, float distance) const {
        if (layered_) return {};   // 층 모양은 면 번호가 피처와 무관 — 높이·치수 편집으로
        if (faceId >= faces_.size() || !std::isfinite(distance) || std::abs(distance) <= kTol)
            return {};
        const auto& face = faces_[faceId];

        // 원통의 해석적 옆면은 한 방향의 평면 법선이 없다. 옆면을 바깥쪽으로
        // 미는 거리는 곧 반지름 변화이며, 중심과 높이는 그대로 유지한다.
        if (feature_.kind == FeatureKind::Cylinder && faceId == 2 &&
            face.surface.kind == BRepSurfaceKind::Cylinder) {
            const float radius = feature_.radius + distance;
            if (radius <= kTol) return {};
            return makeCylinder(radius, feature_.height, feature_.origin);
        }
        if (face.surface.kind != BRepSurfaceKind::Plane) return {};

        switch (feature_.kind) {
        case FeatureKind::Box: {
            const glm::vec3 n = face.surface.normal;
            int axis = 0;
            if (std::abs(n.y) > std::abs(n.x)) axis = 1;
            if (std::abs(n.z) > std::abs(axis == 0 ? n.x : n.y)) axis = 2;
            glm::vec3 dimensions = feature_.dimensions;
            float& d = axis == 0 ? dimensions.x : axis == 1 ? dimensions.y : dimensions.z;
            d += distance;
            if (d <= kTol) return {};
            return makeBox(dimensions, feature_.origin + n * (distance * 0.5f));
        }
        case FeatureKind::Cylinder:
            if (faceId > 1) return {};
            if (feature_.height + distance <= kTol) return {};
            return makeCylinder(feature_.radius, feature_.height + distance,
                                feature_.origin + face.surface.normal * (distance * 0.5f));
        case FeatureKind::Extrude: {
            if (faceId >= 2) {
                const std::size_t count = feature_.profile.size();
                const std::size_t edgeIndex = static_cast<std::size_t>(faceId - 2);
                if (count < 3 || edgeIndex >= count) return {};

                const glm::vec3 direction = glm::normalize(feature_.direction);
                const glm::vec3 u = perpendicular(direction);
                const glm::vec3 v = glm::cross(direction, u);
                std::vector<glm::dvec2> points;
                points.reserve(count);
                for (const auto& point : feature_.profile)
                    points.emplace_back(glm::dot(point, u), glm::dot(point, v));

                const std::size_t first = edgeIndex;
                const std::size_t second = (first + 1) % count;
                const std::size_t previous = (first + count - 1) % count;
                const std::size_t next = (second + 1) % count;
                const glm::dvec2 edge = points[second] - points[first];
                const double edgeLength = glm::length(edge);
                if (edgeLength <= kTol) return {};

                const glm::vec3 faceNormal3 = face.surface.normal;
                glm::dvec2 outward{glm::dot(faceNormal3, u), glm::dot(faceNormal3, v)};
                const double outwardLength = glm::length(outward);
                if (outwardLength <= 1e-10) return {};
                outward /= outwardLength;
                const glm::dvec2 shiftedFirst = points[first] + outward * static_cast<double>(distance);
                const glm::dvec2 shiftedSecond = points[second] + outward * static_cast<double>(distance);

                glm::dvec2 newFirst{}, newSecond{};
                if (!lineIntersection(points[previous], points[first] - points[previous],
                                      shiftedFirst, shiftedSecond - shiftedFirst, newFirst) ||
                    !lineIntersection(shiftedFirst, shiftedSecond - shiftedFirst,
                                      points[second], points[next] - points[second], newSecond))
                    return {};
                if (glm::dot(newSecond - newFirst, edge) <= 1e-10) return {};

                points[first] = newFirst;
                points[second] = newSecond;
                if (!validSimpleProfile2D(points)) return {};

                auto profile = feature_.profile;
                const float plane = glm::dot(feature_.profile.front(), direction);
                auto lift = [&](const glm::dvec2& point) {
                    return u * static_cast<float>(point.x) + v * static_cast<float>(point.y)
                         + direction * plane;
                };
                profile[first] = lift(newFirst);
                profile[second] = lift(newSecond);
                return makeCutExtrude(profile, direction, feature_.height, remakeCuts(), feature_.bosses);
            }
            if (feature_.height + distance <= kTol) return {};
            auto profile = feature_.profile;
            auto cuts = remakeCuts();   // 관통 컷은 새 높이까지 관통
            auto bosses = feature_.bosses;
            const float newHeight = feature_.height + distance;
            if (faceId == 0) {
                const glm::vec3 shift = -feature_.direction * distance;
                for (auto& point : profile) point += shift;
                for (auto& cut : cuts)
                    for (auto& point : cut.profile) point += shift;
                for (auto& boss : bosses)
                    for (auto& point : boss.profile) point += shift;
            }
            return makeCutExtrude(profile, feature_.direction, newHeight, cuts, bosses);
        }
        }
        return {};
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::makeExtrude(
        const std::vector<glm::vec3>& inputProfile,
        const glm::vec3& inputDirection,
        float inputHeight) {
        return makeCutExtrude(inputProfile, inputDirection, inputHeight, {});
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::makeCutExtrude(
        const std::vector<glm::vec3>& inputProfile,
        const glm::vec3& inputDirection,
        float inputHeight,
        const std::vector<CutData>& inputCuts,
        const std::vector<BossData>& inputBosses) {
        if (!finiteVec(inputDirection) || glm::length(inputDirection) <= kTol ||
            !std::isfinite(inputHeight) || std::abs(inputHeight) <= kTol)
            return {};

        std::vector<glm::vec3> profile = cleanProfile(inputProfile);
        if (profile.size() < 3) return {};
        glm::vec3 direction = glm::normalize(inputDirection);
        float height = inputHeight;
        if (height < 0.0f) { direction = -direction; height = -height; }

        const glm::vec3 origin = profile.front();
        float scale = 1.0f;
        for (const auto& p : profile) {
            scale = std::max(scale, glm::length(p - origin));
            if (std::abs(glm::dot(p - origin, direction)) > kTol * scale) return {};
        }
        double signedArea = profileArea(profile, direction);
        if (std::abs(signedArea) <= static_cast<double>(kTol * kTol)) return {};
        if (signedArea < 0.0) std::reverse(profile.begin(), profile.end());

        const glm::vec3 u = perpendicular(direction);
        const glm::vec3 v = glm::cross(direction, u);
        const auto outer2 = projectProfile2D(profile, u, v);
        if (!validSimpleProfile2D(outer2)) return {};
        glm::dvec2 minimum = outer2.front(), maximum = outer2.front();
        for (const auto& point : outer2) {
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        }
        const double profileScale = std::max(1.0, glm::length(maximum - minimum));
        const double lengthTolerance = 1e-7 * profileScale;
        const double areaTolerance = 1e-10 * profileScale * profileScale;

        // 보스·컷 — 단면이 올바른지는 늘 본다. 서로의 자리(외곽 안에 완전히·같은 쪽 보스끼리 떨어짐·컷은 보스 안 또는 밖)가
        // 맞으면 단순 위상(예전 번호 그대로), 어긋나면 층 모양(3D 불리언 → 면 되짚기, buildLayered)으로 간다.
        bool simple = true;
        std::vector<BossData> bosses;
        std::vector<std::vector<glm::dvec2>> bosses2;
        const float flip = inputHeight < 0.0f ? -1.0f : 1.0f;   // 방향을 뒤집었으면 위/아래도 바뀐다
        auto strictlyInside = [&](const std::vector<glm::dvec2>& inner, const std::vector<glm::dvec2>& outer) {
            for (const auto& point : inner)
                if (!pointInsideProfile2D(point, outer, lengthTolerance, areaTolerance)) return false;
            for (std::size_t i = 0; i < inner.size(); ++i)
                for (std::size_t j = 0; j < outer.size(); ++j)
                    if (segmentsIntersect(inner[i], inner[(i + 1) % inner.size()],
                                          outer[j], outer[(j + 1) % outer.size()],
                                          lengthTolerance, areaTolerance)) return false;
            return true;
        };
        auto overlaps = [&](const std::vector<glm::dvec2>& a, const std::vector<glm::dvec2>& b) {
            return !disjointProfiles2D(a, b, lengthTolerance, areaTolerance);
        };
        for (const auto& inputBoss : inputBosses) {
            if (!std::isfinite(inputBoss.height) || std::abs(inputBoss.height) <= kTol) return {};
            BossData boss;
            boss.height = inputBoss.height * flip;
            boss.profile = cleanProfile(inputBoss.profile);
            if (boss.profile.size() < 3) return {};
            for (auto& point : boss.profile) point -= direction * glm::dot(point - origin, direction);
            const double bossArea = profileArea(boss.profile, direction);
            if (std::abs(bossArea) <= static_cast<double>(kTol * kTol)) return {};
            if (bossArea < 0.0) std::reverse(boss.profile.begin(), boss.profile.end());
            auto boss2 = projectProfile2D(boss.profile, u, v);
            if (!validSimpleProfile2D(boss2)) return {};
            if (!overlaps(boss2, outer2)) return {};   // 캡에 닿지 않고 떠 있는 보스 — 덩어리가 따로 논다
            if (!strictlyInside(boss2, outer2)) simple = false;
            for (std::size_t k = 0; k < bosses.size(); ++k)
                if ((bosses[k].height > 0.0f) == (boss.height > 0.0f) && overlaps(boss2, bosses2[k])) simple = false;
            bosses.push_back(std::move(boss));
            bosses2.push_back(std::move(boss2));
        }

        std::vector<CutData> cuts;
        std::vector<std::vector<glm::dvec2>> cuts2;
        std::vector<int> cutTopBoss, cutBottomBoss;   // 단순 위상에서 컷이 들어 있는 보스(없으면 −1)
        cuts.reserve(inputCuts.size());
        cuts2.reserve(inputCuts.size());
        for (const auto& inputCut : inputCuts) {
            if (!std::isfinite(inputCut.depth) || inputCut.depth <= kTol) return {};
            CutData cut;
            cut.profile = cleanProfile(inputCut.profile);
            if (cut.profile.size() < 3) return {};
            for (auto& point : cut.profile) {
                // 호출자는 위 캡 위의 스케치를 넘겨도 된다. 생성 파라미터는 아래
                // 기준면으로 정규화해 높이 편집과 저장/복원을 단순하게 유지한다.
                point -= direction * glm::dot(point - origin, direction);
            }
            double cutArea = profileArea(cut.profile, direction);
            if (std::abs(cutArea) <= static_cast<double>(kTol * kTol)) return {};
            if (cutArea < 0.0) std::reverse(cut.profile.begin(), cut.profile.end());
            auto cut2 = projectProfile2D(cut.profile, u, v);
            if (!validSimpleProfile2D(cut2)) return {};

            bool touchesSolid = overlaps(cut2, outer2);
            for (const auto& b2 : bosses2) touchesSolid = touchesSolid || overlaps(cut2, b2);
            if (!touchesSolid) return {};   // 솔리드 밖 허공을 파는 컷
            if (!strictlyInside(cut2, outer2)) simple = false;
            for (const auto& existing : cuts2)
                if (overlaps(cut2, existing)) simple = false;
            // 기둥 — 위 끝은 겹치는 위 보스 중 가장 높은 끝, 아래 끝은 겹치는 아래 보스 중 가장 낮은 끝
            int topBoss = -1, bottomBoss = -1;
            float topExtra = 0.0f, bottomLevel = 0.0f;
            for (std::size_t k = 0; k < bosses.size(); ++k) {
                if (!overlaps(cut2, bosses2[k])) continue;
                if (!strictlyInside(cut2, bosses2[k])) simple = false;   // 보스 테두리를 가로지름
                if (bosses[k].height > 0.0f) {
                    if (topBoss >= 0) simple = false;
                    topBoss = static_cast<int>(k); topExtra = std::max(topExtra, bosses[k].height);
                } else {
                    if (bottomBoss >= 0) simple = false;
                    bottomBoss = static_cast<int>(k); bottomLevel = std::min(bottomLevel, bosses[k].height);
                }
            }
            const float top = height + topExtra;
            cut.depth = std::min(inputCut.depth, top - bottomLevel);
            cuts.push_back(std::move(cut));
            cuts2.push_back(std::move(cut2));
            cutTopBoss.push_back(topBoss);
            cutBottomBoss.push_back(bottomBoss);
        }

        if (!simple) return LotBRepShapeBuilder::buildLayered(profile, direction, height, cuts, bosses);

        auto shape = std::shared_ptr<LotBRepShape>(new LotBRepShape());
        shape->feature_.kind = FeatureKind::Extrude;
        shape->feature_.height = height;
        shape->feature_.direction = direction;
        shape->feature_.profile = profile;
        shape->feature_.cuts = cuts;
        shape->feature_.bosses = bosses;

        const BRepId count = static_cast<BRepId>(profile.size());
        for (const auto& point : profile)
            shape->vertices_.push_back({static_cast<BRepId>(shape->vertices_.size()), point});
        for (const auto& point : profile)
            shape->vertices_.push_back({static_cast<BRepId>(shape->vertices_.size()), point + direction * height});

        LotBRepShapeBuilder builder{*shape};
        std::vector<BRepId> bottom, top;
        bottom.reserve(count); top.reserve(count);
        for (BRepId i = 0; i < count; ++i) {
            bottom.push_back(count - 1 - i);
            top.push_back(count + i);
        }
        const BRepId bottomFace = builder.face(bottom);
        const BRepId topFace = builder.face(top);
        for (BRepId i = 0; i < count; ++i) {
            const BRepId j = (i + 1) % count;
            builder.face({i, j, count + j, count + i});
        }

        // 컷 — 보스 끝 캡에 뚫리는 구멍은 보스 면을 만든 뒤에 붙인다(보스 없는 모양은 예전과 같은 번호 순서)
        struct PendingHole { int boss; std::vector<BRepId> ring; };
        std::vector<PendingHole> pendingHoles;
        for (std::size_t c = 0; c < cuts.size(); ++c) {
            const auto& cut = cuts[c];
            const BRepId cutCount = static_cast<BRepId>(cut.profile.size());
            const float columnTop = height + (cutTopBoss[c] >= 0 ? bosses[cutTopBoss[c]].height : 0.0f);
            const float columnBottom = cutBottomBoss[c] >= 0 ? bosses[cutBottomBoss[c]].height : 0.0f;
            const float lowerHeight = columnTop - cut.depth;
            std::vector<BRepId> lower, upper;
            lower.reserve(cutCount);
            upper.reserve(cutCount);
            for (const auto& point : cut.profile) {
                lower.push_back(static_cast<BRepId>(shape->vertices_.size()));
                shape->vertices_.push_back({lower.back(), point + direction * lowerHeight});
            }
            for (const auto& point : cut.profile) {
                upper.push_back(static_cast<BRepId>(shape->vertices_.size()));
                shape->vertices_.push_back({upper.back(), point + direction * columnTop});
            }

            std::vector<BRepId> topHole(upper.rbegin(), upper.rend());
            if (cutTopBoss[c] >= 0) pendingHoles.push_back({cutTopBoss[c], topHole});
            else builder.appendLoop(topFace, topHole);
            const bool through = lowerHeight <= columnBottom + kTol;
            if (through) {
                if (cutBottomBoss[c] >= 0) pendingHoles.push_back({cutBottomBoss[c], lower});
                else builder.appendLoop(bottomFace, lower);
            } else {
                builder.face(lower); // 포켓 바닥: 빈 공간을 향하는 +direction 법선
            }
            for (BRepId i = 0; i < cutCount; ++i) {
                const BRepId j = (i + 1) % cutCount;
                // 컷 벽의 솔리드 바깥 방향은 구멍 안쪽이다.
                builder.face({lower[j], lower[i], upper[i], upper[j]});
            }
        }

        // 보스 — 붙는 캡에 구멍 고리, 옆면, 끝 캡
        std::vector<BRepId> bossEndFace(bosses.size(), kInvalidBRepId);
        for (std::size_t k = 0; k < bosses.size(); ++k) {
            const auto& boss = bosses[k];
            const bool onTop = boss.height > 0.0f;
            const float nearLevel = onTop ? height : 0.0f;
            const float farLevel = onTop ? height + boss.height : boss.height;
            const BRepId bossCount = static_cast<BRepId>(boss.profile.size());
            std::vector<BRepId> nearRing, farRing;
            for (const auto& point : boss.profile) {
                nearRing.push_back(static_cast<BRepId>(shape->vertices_.size()));
                shape->vertices_.push_back({nearRing.back(), point + direction * nearLevel});
            }
            for (const auto& point : boss.profile) {
                farRing.push_back(static_cast<BRepId>(shape->vertices_.size()));
                shape->vertices_.push_back({farRing.back(), point + direction * farLevel});
            }
            if (onTop) {
                builder.appendLoop(topFace, std::vector<BRepId>(nearRing.rbegin(), nearRing.rend()));
                bossEndFace[k] = builder.face(farRing);
                for (BRepId i = 0; i < bossCount; ++i) {
                    const BRepId j = (i + 1) % bossCount;
                    builder.face({nearRing[i], nearRing[j], farRing[j], farRing[i]});
                }
            } else {
                builder.appendLoop(bottomFace, nearRing);
                bossEndFace[k] = builder.face(std::vector<BRepId>(farRing.rbegin(), farRing.rend()));
                for (BRepId i = 0; i < bossCount; ++i) {
                    const BRepId j = (i + 1) % bossCount;
                    builder.face({farRing[i], farRing[j], nearRing[j], nearRing[i]});
                }
            }
        }
        for (const auto& hole : pendingHoles) builder.appendLoop(bossEndFace[hole.boss], hole.ring);

        if (!shape->validateClosed()) return {};
        return shape;
    }

    bool LotBRepShape::cutSpan(std::size_t cutIndex, float& top, float& bottom) const {
        if (feature_.kind != FeatureKind::Extrude || cutIndex >= feature_.cuts.size()) return false;
        top = feature_.height;
        bottom = 0.0f;
        const glm::vec3 u = perpendicular(feature_.direction);
        const glm::vec3 v = glm::cross(feature_.direction, u);
        const auto& cut = feature_.cuts[cutIndex];
        if (cut.profile.size() < 3) return true;
        // 겹치는 위 보스 중 가장 높은 끝·아래 보스 중 가장 낮은 끝(만들 때와 같은 규칙)
        const auto cut2 = projectProfile2D(cut.profile, u, v);
        double scale = 1.0;
        for (const auto& p : feature_.profile) scale = std::max(scale, static_cast<double>(glm::length(p - feature_.profile.front())));
        for (const auto& boss : feature_.bosses) {
            if (disjointProfiles2D(cut2, projectProfile2D(boss.profile, u, v), 1e-7 * scale, 1e-10 * scale * scale)) continue;
            if (boss.height > 0.0f) top = std::max(top, feature_.height + boss.height);
            else bottom = std::min(bottom, boss.height);
        }
        return true;
    }

    bool LotBRepShape::cutThrough(std::size_t cutIndex) const {
        float top = 0.0f, bottom = 0.0f;
        if (!cutSpan(cutIndex, top, bottom)) return false;
        return feature_.cuts[cutIndex].depth >= top - bottom - kTol;
    }

    std::vector<LotBRepShape::CutData> LotBRepShape::remakeCuts() const {
        auto cuts = feature_.cuts;
        for (std::size_t i = 0; i < cuts.size(); ++i)
            if (cutThrough(i)) cuts[i].depth = kThroughDepth;
        return cuts;
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::addBoss(
        const std::vector<glm::vec3>& bossProfile, float bossHeight) const {
        if (feature_.kind != FeatureKind::Extrude || bossProfile.size() < 3) return {};
        auto bosses = feature_.bosses;
        bosses.push_back({bossProfile, bossHeight});
        return makeCutExtrude(feature_.profile, feature_.direction, feature_.height, remakeCuts(), bosses);
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::editBoss(
        std::size_t bossIndex, const std::vector<glm::vec3>& bossProfile, float bossHeight) const {
        if (feature_.kind != FeatureKind::Extrude || bossIndex >= feature_.bosses.size() ||
            bossProfile.size() < 3) return {};
        auto bosses = feature_.bosses;
        bosses[bossIndex] = {bossProfile, bossHeight};
        return makeCutExtrude(feature_.profile, feature_.direction, feature_.height, remakeCuts(), bosses);
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::cutExtrude(
        const std::vector<glm::vec3>& cutProfile,
        float depth,
        bool throughAll) const {
        if (feature_.kind != FeatureKind::Extrude || cutProfile.size() < 3) return {};
        if (!throughAll && (!std::isfinite(depth) || depth <= kTol)) return {};
        auto cuts = remakeCuts();
        cuts.push_back({cutProfile, throughAll ? kThroughDepth : depth});
        return makeCutExtrude(feature_.profile, feature_.direction, feature_.height, cuts, feature_.bosses);
    }

    std::shared_ptr<const LotBRepShape> LotBRepShape::editCut(
        std::size_t cutIndex,
        const std::vector<glm::vec3>& cutProfile,
        float depth,
        bool throughAll) const {
        if (feature_.kind != FeatureKind::Extrude || cutIndex >= feature_.cuts.size() ||
            cutProfile.size() < 3) return {};
        if (!throughAll && (!std::isfinite(depth) || depth <= kTol)) return {};

        auto cuts = remakeCuts();
        cuts[cutIndex] = {cutProfile, throughAll ? kThroughDepth : depth};
        auto result = makeCutExtrude(feature_.profile, feature_.direction, feature_.height, cuts, feature_.bosses);
        // 포켓이라 했는데 기둥 끝까지 닿으면 거부(관통은 따로 고른다)
        if (result && !throughAll && result->cutThrough(cutIndex)) return {};
        return result;
    }

    bool LotBRepShape::validateClosed(std::string* error) const {
        if (edges_.empty() || loops_.empty() || faces_.empty()) {
            setError(error, "empty topology"); return false;
        }
        for (std::size_t i = 0; i < vertices_.size(); ++i) {
            if (vertices_[i].id != i || !finiteVec(vertices_[i].point)) {
                setError(error, "invalid vertex"); return false;
            }
        }
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            const auto& edge = edges_[i];
            if (edge.id != i) { setError(error, "invalid edge id"); return false; }
            if (edge.kind == BRepCurveKind::Line) {
                if (edge.firstVertex >= vertices_.size() || edge.lastVertex >= vertices_.size() ||
                    edge.firstVertex == edge.lastVertex ||
                    glm::length(vertices_[edge.firstVertex].point - vertices_[edge.lastVertex].point) <= kTol) {
                    setError(error, "invalid line edge"); return false;
                }
            } else if (!finiteVec(edge.center) || !unitVec(edge.normal) || !unitVec(edge.xDirection) ||
                       std::abs(glm::dot(edge.normal, edge.xDirection)) > 1e-4f ||
                       !std::isfinite(edge.radius) || edge.radius <= kTol) {
                setError(error, "invalid circle edge"); return false;
            }
        }

        std::vector<unsigned int> loopUses(loops_.size(), 0);
        std::vector<unsigned int> edgeUses(edges_.size(), 0);
        for (std::size_t i = 0; i < loops_.size(); ++i) {
            const auto& loop = loops_[i];
            if (loop.id != i || loop.coedges.empty()) { setError(error, "invalid loop"); return false; }
            for (const auto& coedge : loop.coedges) {
                if (coedge.edge >= edges_.size()) { setError(error, "missing loop edge"); return false; }
            }
            if (loop.coedges.size() > 1) {
                for (std::size_t c = 0; c < loop.coedges.size(); ++c) {
                    const auto& current = loop.coedges[c];
                    const auto& next = loop.coedges[(c + 1) % loop.coedges.size()];
                    const auto& a = edges_[current.edge];
                    const auto& b = edges_[next.edge];
                    if (a.kind != BRepCurveKind::Line || b.kind != BRepCurveKind::Line) {
                        setError(error, "unsupported mixed loop"); return false;
                    }
                    const BRepId end = current.reversed ? a.firstVertex : a.lastVertex;
                    const BRepId start = next.reversed ? b.lastVertex : b.firstVertex;
                    if (end != start) { setError(error, "open loop"); return false; }
                }
            } else if (edges_[loop.coedges.front().edge].kind != BRepCurveKind::Circle) {
                setError(error, "single-edge loop is not closed"); return false;
            }
        }

        for (std::size_t i = 0; i < faces_.size(); ++i) {
            const auto& face = faces_[i];
            if (face.id != i || face.loops.empty() || !finiteVec(face.surface.origin) ||
                !unitVec(face.surface.normal) || !unitVec(face.surface.xDirection) ||
                std::abs(glm::dot(face.surface.normal, face.surface.xDirection)) > 1e-4f) {
                setError(error, "invalid face"); return false;
            }
            if (face.surface.kind == BRepSurfaceKind::Cylinder &&
                (!std::isfinite(face.surface.radius) || face.surface.radius <= kTol)) {
                setError(error, "invalid cylinder surface"); return false;
            }
            for (BRepId loopId : face.loops) {
                if (loopId >= loops_.size()) { setError(error, "missing face loop"); return false; }
                ++loopUses[loopId];
                for (const auto& coedge : loops_[loopId].coedges) ++edgeUses[coedge.edge];
            }
        }
        if (std::any_of(loopUses.begin(), loopUses.end(), [](unsigned int uses) { return uses != 1; })) {
            setError(error, "loop must belong to one face"); return false;
        }
        if (std::any_of(edgeUses.begin(), edgeUses.end(), [](unsigned int uses) { return uses != 2; })) {
            setError(error, "closed solid edge must have two face uses"); return false;
        }
        if (error) error->clear();
        return true;
    }

    double LotBRepShape::volume() const {
        if (layered_) return layeredVolume_;
        switch (feature_.kind) {
        case FeatureKind::Box:
            return static_cast<double>(feature_.dimensions.x) * feature_.dimensions.y * feature_.dimensions.z;
        case FeatureKind::Cylinder:
            return kPi * feature_.radius * feature_.radius * feature_.height;
        case FeatureKind::Extrude: {
            double result = std::abs(profileArea(feature_.profile, feature_.direction)) * feature_.height;
            for (const auto& boss : feature_.bosses)
                result += std::abs(profileArea(boss.profile, feature_.direction)) * std::abs(boss.height);
            for (const auto& cut : feature_.cuts)
                result -= std::abs(profileArea(cut.profile, feature_.direction)) * cut.depth;
            return std::max(0.0, result);
        }
        }
        return 0.0;
    }

    double LotBRepShape::surfaceArea() const {
        if (layered_) return layeredArea_;
        switch (feature_.kind) {
        case FeatureKind::Box: {
            const auto& d = feature_.dimensions;
            return 2.0 * (static_cast<double>(d.x) * d.y + static_cast<double>(d.y) * d.z
                        + static_cast<double>(d.z) * d.x);
        }
        case FeatureKind::Cylinder:
            return 2.0 * kPi * feature_.radius * (feature_.radius + feature_.height);
        case FeatureKind::Extrude: {
            double result = 2.0 * std::abs(profileArea(feature_.profile, feature_.direction))
                          + profilePerimeter(feature_.profile) * feature_.height;
            // 보스: 붙는 캡에서 빠진 넓이 = 끝 캡 넓이, 옆면만 더한다
            for (const auto& boss : feature_.bosses)
                result += profilePerimeter(boss.profile) * std::abs(boss.height);
            for (std::size_t i = 0; i < feature_.cuts.size(); ++i) {
                const auto& cut = feature_.cuts[i];
                if (cutThrough(i))
                    result -= 2.0 * std::abs(profileArea(cut.profile, feature_.direction));
                result += profilePerimeter(cut.profile) * cut.depth;
            }
            return result;
        }
        }
        return 0.0;
    }

} // namespace lot
