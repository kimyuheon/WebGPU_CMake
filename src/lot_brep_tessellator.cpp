#include "lot_brep_tessellator.h"

#include "third_party/mapbox/earcut.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lot {
    namespace {
        constexpr float kTwoPi = 6.28318530717958647692f;

        Vertex vertex(const glm::vec3& position,
                      const glm::vec3& normal,
                      const glm::vec3& color,
                      const std::array<float, 2>& uv) {
            return Vertex::make(position.x, position.y, position.z, color.x, color.y, color.z,
                                normal.x, normal.y, normal.z, uv[0], uv[1]);
        }

        LotModel::Builder tessellateBox(const glm::vec3& dimensions,
                                        const glm::vec3& origin,
                                        const glm::vec3& color) {
            LotModel::Builder builder;
            const glm::vec3 half = dimensions * 0.5f;
            struct Face { glm::vec3 normal, u, v; };
            const Face faces[6] = {
                {{ 1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
                {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
                {{ 0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
                {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},
                {{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
                {{ 0, 0,-1}, {0, 1, 0}, {1, 0, 0}},
            };
            for (const auto& face : faces) {
                const glm::vec3 center = origin + face.normal *
                    (face.normal.x != 0.0f ? half.x : face.normal.y != 0.0f ? half.y : half.z);
                const glm::vec3 du = face.u *
                    (face.u.x != 0.0f ? half.x : face.u.y != 0.0f ? half.y : half.z);
                const glm::vec3 dv = face.v *
                    (face.v.x != 0.0f ? half.x : face.v.y != 0.0f ? half.y : half.z);
                const auto base = static_cast<std::uint32_t>(builder.vertices.size());
                builder.vertices.push_back(vertex(center - du - dv, face.normal, color, {0, 0}));
                builder.vertices.push_back(vertex(center + du - dv, face.normal, color, {1, 0}));
                builder.vertices.push_back(vertex(center + du + dv, face.normal, color, {1, 1}));
                builder.vertices.push_back(vertex(center - du + dv, face.normal, color, {0, 1}));
                builder.indices.insert(builder.indices.end(), {
                    base, base + 1, base + 2,
                    base, base + 2, base + 3
                });
            }
            return builder;
        }

        LotModel::Builder tessellateCylinder(float radius, float height, const glm::vec3& origin, int segments,
                                             const glm::vec3& color) {
            LotModel::Builder builder;
            const float halfHeight = height * 0.5f;
            for (int segment = 0; segment <= segments; ++segment) {
                const float u = static_cast<float>(segment) / segments;
                const float angle = (segment == segments) ? 0.0f : u * kTwoPi;
                const glm::vec3 normal{std::cos(angle), std::sin(angle), 0.0f};
                const glm::vec3 radial = normal * radius;
                builder.vertices.push_back(vertex(origin + glm::vec3{radial.x, radial.y, -halfHeight}, normal, color, {u, 0}));
                builder.vertices.push_back(vertex(origin + glm::vec3{radial.x, radial.y,  halfHeight}, normal, color, {u, 1}));
            }
            for (int segment = 0; segment < segments; ++segment) {
                const std::uint32_t base = static_cast<std::uint32_t>(segment * 2);
                builder.indices.insert(builder.indices.end(), {
                    base, base + 2, base + 1,
                    base + 1, base + 2, base + 3
                });
            }

            const std::uint32_t topCenter = static_cast<std::uint32_t>(builder.vertices.size());
            builder.vertices.push_back(vertex(origin + glm::vec3{0, 0, halfHeight}, {0, 0, 1}, color, {0.5f, 0.5f}));
            for (int segment = 0; segment <= segments; ++segment) {
                const float angle = (segment == segments) ? 0.0f
                    : static_cast<float>(segment) / segments * kTwoPi;
                builder.vertices.push_back(vertex(
                    origin + glm::vec3{radius * std::cos(angle), radius * std::sin(angle), halfHeight},
                    {0, 0, 1}, color, {0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)}));
            }
            for (int segment = 0; segment < segments; ++segment)
                builder.indices.insert(builder.indices.end(), {
                    topCenter, topCenter + 1u + static_cast<std::uint32_t>(segment),
                    topCenter + 2u + static_cast<std::uint32_t>(segment)});

            const std::uint32_t bottomCenter = static_cast<std::uint32_t>(builder.vertices.size());
            builder.vertices.push_back(vertex(origin + glm::vec3{0, 0, -halfHeight}, {0, 0, -1}, color, {0.5f, 0.5f}));
            for (int segment = 0; segment <= segments; ++segment) {
                const float angle = (segment == segments) ? 0.0f
                    : static_cast<float>(segment) / segments * kTwoPi;
                builder.vertices.push_back(vertex(
                    origin + glm::vec3{radius * std::cos(angle), radius * std::sin(angle), -halfHeight},
                    {0, 0, -1}, color, {0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)}));
            }
            for (int segment = 0; segment < segments; ++segment)
                builder.indices.insert(builder.indices.end(), {
                    bottomCenter, bottomCenter + 2u + static_cast<std::uint32_t>(segment),
                    bottomCenter + 1u + static_cast<std::uint32_t>(segment)});
            return builder;
        }

        bool appendPlanarFace(LotModel::Builder& builder,
                              const std::vector<std::vector<glm::vec3>>& rings,
                              const glm::vec3& desiredNormal,
                              const glm::vec3& color) {
            if (rings.empty() || rings.front().size() < 3) return false;
            glm::vec3 u = glm::cross(desiredNormal, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::dot(u, u) < 1e-8f)
                u = glm::cross(desiredNormal, glm::vec3(0.0f, 1.0f, 0.0f));
            u = glm::normalize(u);
            const glm::vec3 v = glm::normalize(glm::cross(desiredNormal, u));

            using Point = std::array<double, 2>;
            std::vector<std::vector<Point>> polygon;
            std::vector<glm::vec3> positions;
            polygon.reserve(rings.size());
            for (const auto& ring : rings) {
                if (ring.size() < 3) return false;
                auto& projected = polygon.emplace_back();
                projected.reserve(ring.size());
                for (const auto& point : ring) {
                    projected.push_back({glm::dot(point, u), glm::dot(point, v)});
                    positions.push_back(point);
                }
            }
            const auto triangles = mapbox::earcut<std::uint32_t>(polygon);
            if (triangles.empty()) return false;

            const auto base = static_cast<std::uint32_t>(builder.vertices.size());
            for (const auto& point : positions)
                builder.vertices.push_back(vertex(point, desiredNormal, color,
                    {glm::dot(point, u), glm::dot(point, v)}));
            for (std::size_t i = 0; i + 2 < triangles.size(); i += 3) {
                std::uint32_t a = triangles[i], b = triangles[i + 1], c = triangles[i + 2];
                if (a >= positions.size() || b >= positions.size() || c >= positions.size()) return false;
                if (glm::dot(glm::cross(positions[b] - positions[a], positions[c] - positions[a]),
                             desiredNormal) < 0.0f)
                    std::swap(b, c);
                builder.indices.insert(builder.indices.end(), {base + a, base + b, base + c});
            }
            return true;
        }

        std::optional<LotModel::Builder> tessellateExtrude(
            const LotBRepShape::FeatureData& feature,
            const glm::vec3& color) {
            const auto& profile = feature.profile;
            if (profile.size() < 3 || feature.height <= 0.0f) return std::nullopt;

            const glm::vec3 normal = glm::normalize(feature.direction);
            LotModel::Builder builder;
            const auto count = static_cast<std::uint32_t>(profile.size());
            const glm::vec3 heightVector = normal * feature.height;

            std::vector<std::vector<glm::vec3>> bottomRings{profile};
            std::vector<std::vector<glm::vec3>> topRings;
            topRings.emplace_back();
            topRings.front().reserve(profile.size());
            for (const auto& point : profile) topRings.front().push_back(point + heightVector);
            for (const auto& cut : feature.cuts) {
                std::vector<glm::vec3> top;
                top.reserve(cut.profile.size());
                for (const auto& point : cut.profile) top.push_back(point + heightVector);
                topRings.push_back(std::move(top));
                if (cut.depth >= feature.height - 1e-5f) bottomRings.push_back(cut.profile);
            }
            if (!appendPlanarFace(builder, bottomRings, -normal, color) ||
                !appendPlanarFace(builder, topRings, normal, color)) return std::nullopt;

            float runU = 0.0f;
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::uint32_t next = (i + 1) % count;
                const glm::vec3 bottom0 = profile[i];
                const glm::vec3 bottom1 = profile[next];
                const glm::vec3 top0 = bottom0 + heightVector;
                const glm::vec3 top1 = bottom1 + heightVector;
                const glm::vec3 edge = bottom1 - bottom0;
                const float edgeLength = glm::length(edge);
                if (edgeLength <= 1e-7f) return std::nullopt;
                const glm::vec3 sideNormal = glm::normalize(glm::cross(edge, normal));
                const float nextU = runU + edgeLength;
                const std::uint32_t base = static_cast<std::uint32_t>(builder.vertices.size());
                builder.vertices.push_back(vertex(bottom0, sideNormal, color, {runU, 0.0f}));
                builder.vertices.push_back(vertex(bottom1, sideNormal, color, {nextU, 0.0f}));
                builder.vertices.push_back(vertex(top1, sideNormal, color, {nextU, feature.height}));
                builder.vertices.push_back(vertex(top0, sideNormal, color, {runU, feature.height}));
                builder.indices.insert(builder.indices.end(), {
                    base, base + 1, base + 2,
                    base, base + 2, base + 3
                });
                runU = nextU;
            }

            for (const auto& cut : feature.cuts) {
                const float lowerHeight = feature.height - cut.depth;
                const glm::vec3 lowerVector = normal * lowerHeight;
                if (cut.depth < feature.height - 1e-5f) {
                    std::vector<std::vector<glm::vec3>> floor(1);
                    floor.front().reserve(cut.profile.size());
                    for (const auto& point : cut.profile) floor.front().push_back(point + lowerVector);
                    if (!appendPlanarFace(builder, floor, normal, color)) return std::nullopt;
                }
                float cutRunU = 0.0f;
                for (std::size_t i = 0; i < cut.profile.size(); ++i) {
                    const std::size_t next = (i + 1) % cut.profile.size();
                    const glm::vec3 lower0 = cut.profile[i] + lowerVector;
                    const glm::vec3 lower1 = cut.profile[next] + lowerVector;
                    const glm::vec3 upper0 = cut.profile[i] + heightVector;
                    const glm::vec3 upper1 = cut.profile[next] + heightVector;
                    const glm::vec3 edge = cut.profile[next] - cut.profile[i];
                    const float edgeLength = glm::length(edge);
                    if (edgeLength <= 1e-7f) return std::nullopt;
                    const glm::vec3 wallNormal = glm::normalize(glm::cross(normal, edge));
                    const float nextU = cutRunU + edgeLength;
                    const std::uint32_t base = static_cast<std::uint32_t>(builder.vertices.size());
                    builder.vertices.push_back(vertex(lower1, wallNormal, color, {cutRunU, 0.0f}));
                    builder.vertices.push_back(vertex(lower0, wallNormal, color, {nextU, 0.0f}));
                    builder.vertices.push_back(vertex(upper0, wallNormal, color, {nextU, cut.depth}));
                    builder.vertices.push_back(vertex(upper1, wallNormal, color, {cutRunU, cut.depth}));
                    builder.indices.insert(builder.indices.end(), {
                        base, base + 1, base + 2,
                        base, base + 2, base + 3
                    });
                    cutRunU = nextU;
                }
            }
            return builder;
        }

        // 위상 그대로 — 모든 면이 평면·직선 고리일 때(보스가 있는 돌출). 면마다 고리를 모아 구멍째 삼각형화.
        // 법선은 바깥 고리의 뉴얼 법선(오목한 첫 꼭짓점에서도 방향이 맞다).
        std::optional<LotModel::Builder> tessellateFaces(const LotBRepShape& shape, const glm::vec3& color) {
            LotModel::Builder builder;
            const auto& V = shape.vertices();
            const auto& E = shape.edges();
            const auto& L = shape.loops();
            for (const auto& face : shape.faces()) {
                if (face.surface.kind != BRepSurfaceKind::Plane) return std::nullopt;
                std::vector<std::vector<glm::vec3>> rings;
                for (BRepId loopId : face.loops) {
                    auto& ring = rings.emplace_back();
                    for (const auto& coedge : L[loopId].coedges) {
                        const auto& edge = E[coedge.edge];
                        if (edge.kind != BRepCurveKind::Line) return std::nullopt;
                        ring.push_back(V[coedge.reversed ? edge.lastVertex : edge.firstVertex].point);
                    }
                }
                if (rings.empty() || rings.front().size() < 3) return std::nullopt;
                glm::vec3 n(0.0f);
                const auto& outer = rings.front();
                for (std::size_t i = 0; i < outer.size(); ++i) n += glm::cross(outer[i], outer[(i + 1) % outer.size()]);
                if (glm::length(n) <= 1e-12f) return std::nullopt;
                if (!appendPlanarFace(builder, rings, glm::normalize(n), color)) return std::nullopt;
            }
            return builder;
        }
    } // namespace

    std::optional<LotModel::Builder> tessellateBRep(
        const LotBRepShape& shape,
        const BRepTessellationOptions& inputOptions) {
        if (!shape.validateClosed()) return std::nullopt;
        BRepTessellationOptions options = inputOptions;
        options.curvedSegments = std::clamp(options.curvedSegments, 3, 512);

        const auto& feature = shape.feature();
        switch (feature.kind) {
        case LotBRepShape::FeatureKind::Box:
            return tessellateBox(feature.dimensions, feature.origin, options.color);
        case LotBRepShape::FeatureKind::Cylinder:
            return tessellateCylinder(feature.radius, feature.height, feature.origin,
                                      options.curvedSegments, options.color);
        case LotBRepShape::FeatureKind::Extrude:
            if (!feature.bosses.empty() || shape.layered()) return tessellateFaces(shape, options.color);
            return tessellateExtrude(feature, options.color);
        }
        return std::nullopt;
    }

} // namespace lot
