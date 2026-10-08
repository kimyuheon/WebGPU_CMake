#include "lot_brep_shape.h"
#include "lot_brep_tessellator.h"

#include <cmath>
#include <cstdio>
#include <string>

// 네이티브 tests/brep_tests.cpp 를 옮긴 것 (같은 기대값). 웹 빌드는 예외를 잡지 않아서
// 던지는 대신 실패를 세고, 끝에 종료 코드로 알린다. 실행: tools/brep_tests.sh
namespace {
    int gFailures = 0, gChecks = 0;
    void check(bool value, const char* message) {
        ++gChecks;
        if (!value) { ++gFailures; std::printf("FAIL  %s\n", message); }
    }
    void near(double actual, double expected, double tolerance, const char* message) {
        ++gChecks;
        if (std::abs(actual - expected) > tolerance) {
            ++gFailures;
            std::printf("FAIL  %s (got %.9g, expected %.9g)\n", message, actual, expected);
        }
    }
    void checkIds(const lot::LotBRepShape& shape) {
        for (std::size_t i = 0; i < shape.vertices().size(); ++i) check(shape.vertices()[i].id == i, "vertex id");
        for (std::size_t i = 0; i < shape.edges().size(); ++i) check(shape.edges()[i].id == i, "edge id");
        for (std::size_t i = 0; i < shape.loops().size(); ++i) check(shape.loops()[i].id == i, "loop id");
        for (std::size_t i = 0; i < shape.faces().size(); ++i) check(shape.faces()[i].id == i, "face id");
    }
    double signedMeshVolume(const LotModel::Builder& mesh) {
        double result = 0.0;
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            auto at = [&](std::size_t k) {
                const float* p = mesh.vertices[mesh.indices[k]].position;
                return glm::dvec3(p[0], p[1], p[2]);
            };
            const glm::dvec3 a = at(i), b = at(i + 1), c = at(i + 2);
            result += glm::dot(a, glm::cross(b, c)) / 6.0;
        }
        return result;
    }
}

int runTests() {
    using lot::LotBRepShape;

    const auto box = LotBRepShape::makeBox({2.0f, 3.0f, 4.0f});
    check(box != nullptr && box->validateClosed(), "box closed topology");
    check(box->vertices().size() == 8 && box->edges().size() == 12 && box->faces().size() == 6,
          "box topology counts");
    near(box->volume(), 24.0, 1e-9, "box volume");
    near(box->surfaceArea(), 52.0, 1e-9, "box area");
    checkIds(*box);
    const auto boxMesh = lot::tessellateBRep(*box);
    check(boxMesh.has_value() && boxMesh->indices.size() == 36, "box tessellation");
    glm::vec3 boxMin(1e9f), boxMax(-1e9f);
    for (const auto& vertex : boxMesh->vertices) {
        const float* p = vertex.position;
        boxMin = glm::vec3(std::fmin(boxMin.x, p[0]), std::fmin(boxMin.y, p[1]), std::fmin(boxMin.z, p[2]));
        boxMax = glm::vec3(std::fmax(boxMax.x, p[0]), std::fmax(boxMax.y, p[1]), std::fmax(boxMax.z, p[2]));
    }
    check(glm::length((boxMax - boxMin) - glm::vec3(2, 3, 4)) < 1e-6f,
          "box tessellation dimensions");
    near(signedMeshVolume(*boxMesh), box->volume(), 1e-6, "box tessellation winding");
    const auto pushedBox = box->pushPullFace(1, 2.0f);
    check(pushedBox && pushedBox->faces()[1].id == 1, "box face id survives push/pull");
    near(pushedBox->feature().dimensions.z, 6.0, 1e-6, "box push/pull dimension");
    near(pushedBox->feature().origin.z, 1.0, 1e-6, "box push/pull center");
    near(pushedBox->faces()[0].surface.origin.z, -2.0, 1e-6, "box opposite face stays fixed");

    const auto cylinder = LotBRepShape::makeCylinder(2.0f, 5.0f);
    check(cylinder != nullptr && cylinder->validateClosed(), "cylinder closed topology");
    check(cylinder->edges().size() == 2 && cylinder->faces().size() == 3, "cylinder topology counts");
    near(cylinder->volume(), 20.0 * 3.14159265358979323846, 1e-8, "cylinder volume");
    near(cylinder->surfaceArea(), 28.0 * 3.14159265358979323846, 1e-8, "cylinder area");
    checkIds(*cylinder);
    lot::BRepTessellationOptions cylinderOptions;
    cylinderOptions.curvedSegments = 24;
    const auto cylinderMesh = lot::tessellateBRep(*cylinder, cylinderOptions);
    check(cylinderMesh.has_value() && cylinderMesh->indices.size() == 24u * 12u,
          "cylinder tessellation");
    near(signedMeshVolume(*cylinderMesh), cylinder->volume(), 1.0,
         "cylinder tessellation winding");
    const auto widenedCylinder = cylinder->pushPullFace(2, 0.75f);
    check(widenedCylinder && widenedCylinder->faces()[2].id == 2,
          "cylinder side face id survives push/pull");
    near(widenedCylinder->feature().radius, 2.75, 1e-6, "cylinder side push/pull radius");
    near(widenedCylinder->feature().height, 5.0, 1e-6, "cylinder side push/pull keeps height");
    check(glm::length(widenedCylinder->feature().origin - cylinder->feature().origin) < 1e-6f,
          "cylinder side push/pull keeps center");
    check(!cylinder->pushPullFace(2, -2.0f), "cylinder side accepted collapsed radius");

    const std::vector<glm::vec3> rectangle{{-1.0f, -1.5f, 0.0f}, {1.0f, -1.5f, 0.0f},
                                            {1.0f,  1.5f, 0.0f}, {-1.0f, 1.5f, 0.0f}};
    const auto extrude = LotBRepShape::makeExtrude(rectangle, {0, 0, 1}, 4.0f);
    check(extrude != nullptr && extrude->validateClosed(), "extrude closed topology");
    check(extrude->vertices().size() == 8 && extrude->edges().size() == 12 && extrude->faces().size() == 6,
          "extrude topology counts");
    near(extrude->volume(), 24.0, 1e-7, "extrude volume");
    near(extrude->surfaceArea(), 52.0, 1e-7, "extrude area");
    const auto pushedExtrude = extrude->pushPullFace(0, 1.0f);
    check(pushedExtrude && pushedExtrude->faces()[0].id == 0, "extrude cap id survives push/pull");
    near(pushedExtrude->feature().height, 5.0, 1e-6, "extrude bottom push/pull height");
    near(pushedExtrude->feature().profile.front().z, -1.0, 1e-6, "extrude bottom cap moved");
    near(pushedExtrude->feature().profile.front().z + pushedExtrude->feature().height,
         4.0, 1e-6, "extrude opposite cap stays fixed");
    const auto widenedExtrude = extrude->pushPullFace(2, 0.5f);
    check(widenedExtrude && widenedExtrude->faces()[2].id == 2,
          "extrude side face id survives push/pull");
    near(widenedExtrude->feature().height, 4.0, 1e-6,
         "extrude side push/pull keeps height");
    near(widenedExtrude->feature().profile[0].y, -2.0, 1e-6,
         "extrude side push/pull moves first endpoint");
    near(widenedExtrude->feature().profile[1].y, -2.0, 1e-6,
         "extrude side push/pull moves second endpoint");
    near(widenedExtrude->feature().profile[2].y, 1.5, 1e-6,
         "extrude opposite side stays fixed");
    near(widenedExtrude->volume(), 28.0, 1e-6, "extrude side push/pull volume");
    near(widenedExtrude->surfaceArea(), 58.0, 1e-6, "extrude side push/pull area");
    check(!extrude->pushPullFace(2, -3.0f), "extrude side accepted collapsed profile");

    const std::vector<glm::vec3> trapezoid{{0,0,0},{4,0,0},{3,2,0},{1,2,0}};
    const auto trapezoidExtrude = LotBRepShape::makeExtrude(trapezoid, {0,0,1}, 1.0f);
    const auto pushedTrapezoid = trapezoidExtrude->pushPullFace(2, 1.0f);
    check(pushedTrapezoid != nullptr, "non-orthogonal extrude side push/pull");
    near(pushedTrapezoid->feature().profile[0].x, -0.5, 1e-6,
         "extrude side intersects previous plane");
    near(pushedTrapezoid->feature().profile[0].y, -1.0, 1e-6,
         "extrude side offset distance");
    near(pushedTrapezoid->feature().profile[1].x, 4.5, 1e-6,
         "extrude side intersects next plane");
    near(pushedTrapezoid->feature().profile[2].x, 3.0, 1e-6,
         "extrude untouched vertex stays fixed");

    const std::vector<glm::vec3> concave{{0,0,0},{2,0,0},{2,1,0},{1,1,0},{1,2,0},{0,2,0}};
    const auto concaveExtrude = LotBRepShape::makeExtrude(concave, {0, 0, 1}, 2.0f);
    check(concaveExtrude != nullptr && concaveExtrude->validateClosed(), "concave extrude topology");
    near(concaveExtrude->volume(), 6.0, 1e-7, "concave extrude volume");
    near(concaveExtrude->surfaceArea(), 22.0, 1e-7, "concave extrude area");
    const auto concaveMesh = lot::tessellateBRep(*concaveExtrude);
    check(concaveMesh.has_value() && concaveMesh->indices.size() == 60,
          "concave extrude tessellation");
    near(signedMeshVolume(*concaveMesh), concaveExtrude->volume(), 1e-6,
         "concave extrude tessellation winding");
    const auto pushedConcave = concaveExtrude->pushPullFace(2, 0.5f);
    check(pushedConcave && pushedConcave->validateClosed(), "concave extrude side push/pull");
    near(pushedConcave->volume(), 8.0, 1e-6, "concave extrude side push/pull volume");
    check(!concaveExtrude->pushPullFace(4, 1.0f),
          "concave extrude accepted a self-touching side push/pull");

    const std::vector<glm::vec3> cutOuter{{-3,-3,0},{3,-3,0},{3,3,0},{-3,3,0}};
    const std::vector<glm::vec3> squareCut{{-1,-1,4},{1,-1,4},{1,1,4},{-1,1,4}};
    const auto cutBase = LotBRepShape::makeExtrude(cutOuter, {0,0,1}, 4.0f);
    const auto throughCut = cutBase->cutExtrude(squareCut, 0.0f, true);
    check(throughCut && throughCut->validateClosed(), "through cut closed topology");
    check(throughCut->feature().cuts.size() == 1 &&
          throughCut->vertices().size() == 16 && throughCut->edges().size() == 24 &&
          throughCut->faces().size() == 10, "through cut topology counts");
    near(throughCut->feature().cuts[0].profile.front().z, 0.0, 1e-6,
         "cut profile projected to base plane");
    near(throughCut->feature().cuts[0].depth, 4.0, 1e-6, "through cut depth");
    near(throughCut->volume(), 128.0, 1e-6, "through cut volume");
    near(throughCut->surfaceArea(), 192.0, 1e-6, "through cut area");
    const auto throughMesh = lot::tessellateBRep(*throughCut);
    check(throughMesh.has_value(), "through cut tessellation");
    near(signedMeshVolume(*throughMesh), throughCut->volume(), 1e-5,
         "through cut tessellation winding");

    const auto pocketCut = cutBase->cutExtrude(squareCut, 2.0f, false);
    check(pocketCut && pocketCut->validateClosed(), "pocket cut closed topology");
    check(pocketCut->faces().size() == 11, "pocket cut topology counts");
    near(pocketCut->volume(), 136.0, 1e-6, "pocket cut volume");
    near(pocketCut->surfaceArea(), 184.0, 1e-6, "pocket cut area");
    const auto pocketMesh = lot::tessellateBRep(*pocketCut);
    check(pocketMesh.has_value(), "pocket cut tessellation");
    near(signedMeshVolume(*pocketMesh), pocketCut->volume(), 1e-5,
         "pocket cut tessellation winding");
    const std::vector<glm::vec3> wideCut{{-1.5f,-1,0},{1.5f,-1,0},{1.5f,1,0},{-1.5f,1,0}};
    const auto editedPocket = throughCut->editCut(0, wideCut, 1.5f, false);
    check(editedPocket && editedPocket->validateClosed(), "edit through cut to pocket");
    near(editedPocket->feature().cuts[0].depth, 1.5, 1e-6, "edited pocket depth");
    near(editedPocket->volume(), 135.0, 1e-6, "edited pocket profile volume");
    near(editedPocket->surfaceArea(), 183.0, 1e-6, "edited pocket profile area");
    const auto editedPocketMesh = lot::tessellateBRep(*editedPocket);
    check(editedPocketMesh.has_value(), "edited pocket tessellation");
    near(signedMeshVolume(*editedPocketMesh), editedPocket->volume(), 1e-5,
         "edited pocket tessellation winding");
    const auto editedThrough = editedPocket->editCut(0, wideCut, 0.0f, true);
    check(editedThrough && editedThrough->feature().cuts[0].depth == 4.0f,
          "edit pocket to through cut");
    near(editedThrough->volume(), 120.0, 1e-6, "edited through profile volume");
    check(!editedPocket->editCut(1, wideCut, 1.0f, false), "invalid cut index accepted");
    check(!editedPocket->editCut(0, wideCut, 4.0f, false),
          "pocket depth reaching solid bottom accepted");
    {   // 테두리에 닿는 컷으로 고치면 층 모양(열린 홈)
        const auto openEdit = editedPocket->editCut(0, {{2,-1,0},{3,-1,0},{3,1,0},{2,1,0}}, 1.0f, false);
        check(openEdit && openEdit->layered() && openEdit->validateClosed(), "edited cut touching outer boundary → layered");
        if (openEdit) near(openEdit->volume(), 142.0, 1e-4, "edited open cut volume");
    }
    near(throughCut->volume(), 128.0, 1e-6, "cut edit mutated original shape");
    const auto tallerPocket = pocketCut->pushPullFace(1, 1.0f);
    check(tallerPocket && tallerPocket->feature().cuts.size() == 1,
          "extrude height edit preserves pocket");
    near(tallerPocket->feature().cuts[0].depth, 2.0, 1e-6,
         "pocket depth preserved after top push/pull");
    const auto tallerThrough = throughCut->pushPullFace(1, 1.0f);
    check(tallerThrough && tallerThrough->feature().cuts[0].depth == 5.0f,
          "through cut follows changed height");

    // 테두리에 걸친 컷(열린 홈) — 층 모양: 3D 불리언 → 면 되짚기, 부피·분할 메시 부피 일치, 면 밀당은 막는다
    {
        const auto open = cutBase->cutExtrude({{2,-1,0},{3,-1,0},{3,1,0},{2,1,0}}, 1.0f, false);
        check(open && open->layered() && open->validateClosed(), "open cut touching outer boundary → layered shape");
        if (open) {
            near(open->volume(), 142.0, 1e-4, "open cut volume");
            near(open->surfaceArea(), 168.0 + 2.0, 1e-4, "open cut area");   // 판 168 + 홈 벽(1×1 ×2) − 바닥 넓이 변화 없음
            const auto mesh = lot::tessellateBRep(*open);
            check(mesh.has_value(), "open cut tessellation");
            if (mesh) near(signedMeshVolume(*mesh), open->volume(), 1e-4, "open cut tessellation winding");
            check(!open->pushPullFace(1, 1.0f), "layered shape blocks face push/pull");
        }
        // 축 끝 키 홈 — 6×6×10 기둥 위 끝에서 테두리를 가로지르는 2×(1 안 + 1 밖) 홈 깊이 4
        const auto shaft = LotBRepShape::makeExtrude(cutOuter, {0,0,1}, 10.0f);
        const auto keyway = shaft->cutExtrude({{-1,2,10},{1,2,10},{1,4,10},{-1,4,10}}, 4.0f, false);
        check(keyway && keyway->layered() && keyway->validateClosed() && !keyway->cutThrough(0), "open keyway at the shaft end");
        if (keyway) near(keyway->volume(), 360.0 - 8.0, 1e-4, "open keyway volume");
        const auto longer = keyway ? LotBRepShape::makeCutExtrude(keyway->feature().profile, keyway->feature().direction, 12.0f,
                                                                  keyway->remakeCuts(), keyway->feature().bosses) : nullptr;
        check(longer && longer->layered(), "height edit keeps the open keyway");
        if (longer) near(longer->volume(), 432.0 - 8.0, 1e-4, "longer shaft keeps keyway depth 4");
        // 두 덩어리로 쪼개는 관통 홈·허공 컷은 거부
        check(!cutBase->cutExtrude({{-4,-0.5f,0},{4,-0.5f,0},{4,0.5f,0},{-4,0.5f,0}}, 0.0f, true), "cut splitting the solid in two accepted");
        check(!cutBase->cutExtrude({{5,5,0},{6,5,0},{6,6,0},{5,6,0}}, 1.0f, false), "cut in empty air accepted");
    }
    const auto firstPocket = cutBase->cutExtrude({{-2.5f,-0.5f,0},{-1.5f,-0.5f,0},
                                                   {-1.5f,0.5f,0},{-2.5f,0.5f,0}}, 1.0f, false);
    check(firstPocket != nullptr, "first disjoint pocket");
    {   // 겹친 컷 — 합친 자리만큼(1 + 4 − 0.5 겹침)
        const auto both = firstPocket->cutExtrude({{-2,-1,0},{0,-1,0},{0,1,0},{-2,1,0}}, 1.0f, false);
        check(both && both->layered() && both->validateClosed(), "overlapping cut profiles → layered");
        if (both) near(both->volume(), 144.0 - 4.5, 1e-4, "overlapping cuts volume");
    }

    // 보스(더하는 돌출) — 6×6×4 판 위 2×2×3 기둥, 컷은 기둥 끝에서 파 내려간다
    {
        const std::vector<glm::vec3> bossSquare{{-1,-1,4},{1,-1,4},{1,1,4},{-1,1,4}};
        const std::vector<glm::vec3> holeSquare{{-0.5f,-0.5f,0},{0.5f,-0.5f,0},{0.5f,0.5f,0},{-0.5f,0.5f,0}};
        auto meshMatches = [&](const std::shared_ptr<const LotBRepShape>& shape, const char* what) {
            const auto mesh = shape ? lot::tessellateBRep(*shape) : std::nullopt;
            check(mesh.has_value(), what);
            if (mesh) near(signedMeshVolume(*mesh), shape->volume(), 1e-4, what);
        };
        const auto boss = cutBase->addBoss(bossSquare, 3.0f);
        check(boss && boss->validateClosed(), "top boss closed topology");
        check(boss->faces().size() == 11 && boss->feature().bosses.size() == 1, "top boss topology counts");
        near(boss->feature().bosses[0].profile.front().z, 0.0, 1e-6, "boss profile projected to base plane");
        near(boss->volume(), 156.0, 1e-6, "top boss volume");
        near(boss->surfaceArea(), 192.0, 1e-6, "top boss area");
        meshMatches(boss, "top boss tessellation");

        const auto bossThrough = boss->cutExtrude(holeSquare, 0.0f, true);
        check(bossThrough && bossThrough->validateClosed(), "through cut inside boss");
        near(bossThrough->feature().cuts[0].depth, 7.0, 1e-6, "through cut spans base + boss");
        check(bossThrough->cutThrough(0), "through cut inside boss reported through");
        near(bossThrough->volume(), 149.0, 1e-6, "through cut inside boss volume");
        near(bossThrough->surfaceArea(), 218.0, 1e-6, "through cut inside boss area");
        meshMatches(bossThrough, "through cut inside boss tessellation");

        const auto bossPocket = boss->cutExtrude(holeSquare, 5.0f, false);
        check(bossPocket && bossPocket->validateClosed() && !bossPocket->cutThrough(0), "pocket from boss top into base");
        near(bossPocket->volume(), 151.0, 1e-6, "pocket from boss top volume");
        near(bossPocket->surfaceArea(), 212.0, 1e-6, "pocket from boss top area");
        meshMatches(bossPocket, "pocket from boss top tessellation");

        const auto both = bossThrough->addBoss(bossSquare, -2.0f);
        check(both && both->validateClosed(), "bottom boss under through cut");
        near(both->feature().cuts[0].depth, 9.0, 1e-6, "through cut stays through after bottom boss");
        near(both->volume(), 155.0, 1e-6, "top + bottom boss with through cut volume");
        meshMatches(both, "top + bottom boss tessellation");

        const auto taller = bossThrough->pushPullFace(1, 1.0f);
        check(taller && taller->cutThrough(0), "through cut follows base height with boss");
        near(taller->volume(), 184.0, 1e-6, "base height edit keeps boss");
        const auto edited = boss->editBoss(0, bossSquare, 5.0f);
        check(edited && edited->validateClosed(), "boss height edit");
        near(edited->volume(), 164.0, 1e-6, "boss height edit volume");

        // 테두리 걸침 — 층 모양으로 받는다
        const auto crossing = boss->cutExtrude({{0,-0.5f,0},{2,-0.5f,0},{2,0.5f,0},{0,0.5f,0}}, 1.0f, false);
        check(crossing && crossing->layered(), "cut crossing boss boundary → layered");
        if (crossing) near(crossing->volume(), 155.0, 1e-4, "cut crossing boss boundary volume (only the boss part is cut)");
        meshMatches(crossing, "cut crossing boss tessellation");
        const auto edgeBoss = cutBase->addBoss({{1,-1,0},{3,-1,0},{3,1,0},{1,1,0}}, 1.0f);
        check(edgeBoss && edgeBoss->layered(), "boss touching outer boundary → layered");
        if (edgeBoss) near(edgeBoss->volume(), 148.0, 1e-4, "edge boss volume");
        meshMatches(edgeBoss, "edge boss tessellation");
        const auto merged = boss->addBoss({{0,-1,0},{2,-1,0},{2,1,0},{0,1,0}}, 1.0f);
        check(merged && merged->layered(), "overlapping same-side bosses → layered");
        if (merged) near(merged->volume(), 158.0, 1e-4, "overlapping bosses volume");
        check(!cutBase->addBoss({{4,-1,0},{6,-1,0},{6,1,0},{4,1,0}}, 1.0f), "floating boss off the cap accepted");
        check(!bossThrough->editCut(0, holeSquare, 7.0f, false), "boss pocket reaching bottom accepted");
    }

    check(!LotBRepShape::makeBox({1, 0, 1}), "zero box dimension accepted");
    check(!LotBRepShape::makeCylinder(-1, 2), "negative cylinder radius accepted");
    check(!LotBRepShape::makeExtrude({{0,0,0},{1,0,0},{1,1,0},{0,1,0.1f}}, {0,0,1}, 1),
          "non-planar profile accepted");
    std::vector<glm::vec3> oversizedProfile;
    oversizedProfile.reserve(lot::kMaxBRepProfilePoints + 1);
    for (std::size_t i = 0; i <= lot::kMaxBRepProfilePoints; ++i) {
        const float angle = 2.0f * 3.14159265358979323846f *
                            static_cast<float>(i) /
                            static_cast<float>(lot::kMaxBRepProfilePoints + 1);
        oversizedProfile.emplace_back(std::cos(angle), std::sin(angle), 0.0f);
    }
    check(!LotBRepShape::makeExtrude(oversizedProfile, {0,0,1}, 1.0f),
          "oversized O(n^2) profile accepted");

    std::printf("brep: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

int main() { return runTests(); }
