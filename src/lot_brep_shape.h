#pragma once

// VulkanCAD 자체 Mini B-Rep의 CPU 측 원본 형상.
//
// LotModel은 Vulkan 렌더링을 위한 삼각형 캐시이고, 이 타입은 정밀한 면/모서리 연결과
// 생성 파라미터를 보존한다. 둘을 분리해야 테셀레이션 밀도를 바꿔도 CAD 면의 정체성이
// 유지되고, 이후 치수 재편집과 STEP 계열 변환기를 붙일 수 있다.

// 웹 이식: 네이티브 3dEngine/lot_brep_shape.h 와 같은 코드 (glm 대신 lot_brep_glm.h).
//   빠진 것: transformedVolume / transformedSurfaceArea (mat3 - 아직 쓰는 곳이 없다).
#include "lot_brep_glm.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace lot {

    using BRepId = std::uint32_t;
    inline constexpr BRepId kInvalidBRepId = std::numeric_limits<BRepId>::max();
    // 단순 폐프로파일 검사는 교차 여부를 정확히 확인하기 위해 O(n²)이다. 이 상한은
    // 외부 입력 하나가 편집 메인 스레드를 장시간 점유하지 않게 하는 공개 계약이다.
    inline constexpr std::size_t kMaxBRepProfilePoints = 2048;

    enum class BRepCurveKind { Line, Circle };
    enum class BRepSurfaceKind { Plane, Cylinder };

    struct BRepVertex {
        BRepId id{kInvalidBRepId};
        glm::vec3 point{0.0f};
    };

    // Line은 firstVertex/lastVertex로 정의한다. Circle은 닫힌 해석 곡선이므로 두 정점이
    // kInvalidBRepId이고 center/normal/xDirection/radius를 사용한다.
    struct BRepEdge {
        BRepId id{kInvalidBRepId};
        BRepCurveKind kind{BRepCurveKind::Line};
        BRepId firstVertex{kInvalidBRepId};
        BRepId lastVertex{kInvalidBRepId};
        glm::vec3 center{0.0f};
        glm::vec3 normal{0.0f, 0.0f, 1.0f};
        glm::vec3 xDirection{1.0f, 0.0f, 0.0f};
        float radius{0.0f};
    };

    struct BRepCoedge {
        BRepId edge{kInvalidBRepId};
        bool reversed{false};
    };

    struct BRepLoop {
        BRepId id{kInvalidBRepId};
        std::vector<BRepCoedge> coedges;
    };

    struct BRepSurface {
        BRepSurfaceKind kind{BRepSurfaceKind::Plane};
        glm::vec3 origin{0.0f};
        // Plane에서는 면 법선, Cylinder에서는 축 방향.
        glm::vec3 normal{0.0f, 0.0f, 1.0f};
        glm::vec3 xDirection{1.0f, 0.0f, 0.0f};
        float radius{0.0f};
    };

    struct BRepFace {
        BRepId id{kInvalidBRepId};
        BRepSurface surface{};
        // 첫 루프가 외곽, 이후 루프는 구멍 또는 주기 곡면의 추가 경계다.
        std::vector<BRepId> loops;
    };

    class LotBRepShape {
    public:
        enum class FeatureKind { Box, Cylinder, Extrude };

        struct CutData {
            // Extrude 아래 기준면으로 투영된 폐프로파일. depth는 위 캡에서 아래로
            // 파낸 거리이며 height와 같으면 관통 컷이다.
            std::vector<glm::vec3> profile;
            float depth{0.0f};
        };

        // 보스(더하는 돌출) — 바닥 윤곽 안쪽에 놓인 폐프로파일을 캡에서 밖으로 쌓는다.
        // profile 은 컷처럼 아래 기준면으로 투영해 둔다. height > 0 이면 위 캡에서 +direction 으로,
        // height < 0 이면 아래 캡에서 −direction 으로. 발자국은 외곽 안에 완전히 들어가야 하고
        // 같은 쪽 보스끼리 겹치면 안 된다(윤곽을 공유하는 계단은 아직 못 만든다).
        struct BossData {
            std::vector<glm::vec3> profile;
            float height{0.0f};
        };

        struct FeatureData {
            FeatureKind kind{FeatureKind::Box};
            glm::vec3 origin{0.0f};                // Box/Cylinder 중심
            glm::vec3 dimensions{1.0f};            // Box
            float radius{0.0f};                    // Cylinder
            float height{0.0f};                    // Cylinder / Extrude (항상 양수)
            glm::vec3 direction{0.0f, 0.0f, 1.0f}; // Cylinder / Extrude
            std::vector<glm::vec3> profile;         // Extrude의 아래 경계(로컬)
            std::vector<CutData> cuts;              // Extrude의 포켓/관통 컷
            std::vector<BossData> bosses;           // Extrude 위·아래 캡의 보스
        };

        static std::shared_ptr<const LotBRepShape> makeBox(
            const glm::vec3& dimensions, const glm::vec3& center = glm::vec3(0.0f));
        static std::shared_ptr<const LotBRepShape> makeCylinder(
            float radius, float height, const glm::vec3& center = glm::vec3(0.0f));
        static std::shared_ptr<const LotBRepShape> makeExtrude(
            const std::vector<glm::vec3>& profile,
            const glm::vec3& direction,
            float height);
        static std::shared_ptr<const LotBRepShape> makeCutExtrude(
            const std::vector<glm::vec3>& profile,
            const glm::vec3& direction,
            float height,
            const std::vector<CutData>& cuts,
            const std::vector<BossData>& bosses = {});

        // 기존 Extrude에 컷 하나를 추가한다. cutProfile은 아래/위 캡과 평행한 어느
        // 평면에 있어도 아래 기준면으로 투영한다. throughAll이면 depth를 무시한다.
        // 외곽과 닿거나 다른 컷과 겹치는 프로파일은 거부한다.
        std::shared_ptr<const LotBRepShape> cutExtrude(
            const std::vector<glm::vec3>& cutProfile,
            float depth,
            bool throughAll) const;

        // 기존 컷의 폐프로파일과 깊이를 교체한다. 인덱스는 feature().cuts의 안정적인
        // 순서이며, throughAll이면 새 높이에 맞춰 끝까지 관통한다. 외곽을 벗어나거나
        // 다른 컷과 겹치는 편집은 nullptr을 반환하고 원본 형상은 바꾸지 않는다.
        std::shared_ptr<const LotBRepShape> editCut(
            std::size_t cutIndex,
            const std::vector<glm::vec3>& cutProfile,
            float depth,
            bool throughAll) const;

        // 보스 하나를 더한다. bossProfile 은 캡과 평행한 어느 평면에 있어도 아래 기준면으로 투영한다.
        // height > 0 = 위 캡에서 위로, < 0 = 아래 캡에서 아래로. 외곽에 닿거나 같은 쪽 보스와 겹치거나
        // 기존 컷의 테두리를 가로지르면 nullptr.
        std::shared_ptr<const LotBRepShape> addBoss(
            const std::vector<glm::vec3>& bossProfile,
            float height) const;
        std::shared_ptr<const LotBRepShape> editBoss(
            std::size_t bossIndex,
            const std::vector<glm::vec3>& bossProfile,
            float height) const;

        // 컷 i 가 지나는 기둥 — 위 끝(위 보스 안이면 보스 끝)·아래 끝(아래 보스 안이면 보스 끝), 기준면에서의 높이.
        // 컷은 위 끝에서 depth 만큼 파 내려가고 depth 가 기둥 길이면 관통이다.
        bool cutSpan(std::size_t cutIndex, float& top, float& bottom) const;
        bool cutThrough(std::size_t cutIndex) const;
        // 관통 컷에 넘기는 깊이 — 만들 때 기둥 길이로 잘린다(높이·보스가 바뀌어도 관통 유지).
        static constexpr float kThroughDepth = 3.0e38f;
        // 다시 만들 때 넘길 컷 목록 — 관통 컷은 kThroughDepth 로 바꿔 둔다(높이·보스가 바뀌어도 관통 유지).
        std::vector<CutData> remakeCuts() const;

        // 지정한 해석 면을 바깥쪽 법선 방향으로 이동한다. 원통 옆면은 반지름을 바꾸고,
        // 돌출체 옆면은 프로파일 변과 인접 변의 교차점을 갱신한다.
        // 자기교차하거나 솔리드가 사라지는 거리면 nullptr을 반환한다.
        // 재생성 후에도 face id 순서는 유지된다.
        std::shared_ptr<const LotBRepShape> pushPullFace(BRepId faceId, float distance) const;

        // 층 모양 — 보스·컷이 외곽이나 서로의 테두리에 걸쳐(축 끝의 열린 키 홈, 가장자리 홈, 넘치는 보스) 단순 위상으로
        // 못 짜서, 기둥들을 3D 불리언(Manifold)으로 합치고 뺀 결과 메시에서 같은 평면 삼각형을 면으로 묶어 되짚은 것.
        // 면·모서리 번호가 피처 순서와 무관하다 — 면 밀당은 막고(높이·치수·스케치 재생성은 된다), 부피·넓이는 결과에서.
        bool layered() const { return layered_; }

        const FeatureData& feature() const { return feature_; }
        const std::vector<BRepVertex>& vertices() const { return vertices_; }
        const std::vector<BRepEdge>& edges() const { return edges_; }
        const std::vector<BRepLoop>& loops() const { return loops_; }
        const std::vector<BRepFace>& faces() const { return faces_; }

        // 모든 루프가 이어지고 각 모서리가 정확히 두 면에서 사용되는 닫힌 솔리드인지 검사한다.
        bool validateClosed(std::string* error = nullptr) const;

        // 생성 파라미터의 해석적 값. 렌더 메시의 분할 수에 영향을 받지 않는다.
        double volume() const;
        double surfaceArea() const;

    private:
        friend struct LotBRepShapeBuilder;
        LotBRepShape() = default;

        FeatureData feature_{};
        bool layered_{false};
        double layeredVolume_{0.0}, layeredArea_{0.0};
        std::vector<BRepVertex> vertices_;
        std::vector<BRepEdge> edges_;
        std::vector<BRepLoop> loops_;
        std::vector<BRepFace> faces_;
    };

} // namespace lot
