#pragma once

#include "lot_game_object.h"
#include "lot_math.h"

#include <string>
#include <utility>
#include <vector>

class LineRenderSystem;
class LotCamera;
class TextRenderSystem;

// 치수 (정렬 치수, AutoCAD DIMALIGNED).
//
// 두 측정점 p1, p2 와 치수선이 지나는 점 dimLine 으로 정의된다. 보조선은 측정점에서
// 치수선까지, 치수선은 두 보조선 사이, 화살표는 치수선 양 끝, 값 글자는 치수선
// 가운데 위. 전부 작업평면(normal) 안에 눕는다. Vulkan 쪽 DimensionData 의 Aligned
// 타입과 같은 정의고 .lot 의 "dim" 키로 오간다 (Linear/Angular/Radius 는 아직 없다).
//
// 지오메트리는 저장하지 않는다 - 매 프레임 정의에서 다시 그린다 (문자는 텍스처 캐시).
namespace lot_dim {

// 렌더 지오메트리. 치수 오브젝트를 그리는 쪽과 집는 쪽이 같은 걸 쓴다.
struct Geometry {
    std::vector<std::pair<vec3, vec3>> segments;  // 보조선 2 + 치수선 1 + 화살표 4
    vec3 textOrigin{};
    vec3 textRight{};
    vec3 textUp{};
    std::string text;
    float textHeight = 0.0f;
    float value = 0.0f;
};

// 월드 좌표 정의 (로컬 dim 에 오브젝트 변환을 적용한 것) 에서 지오메트리를 만든다.
// camera 를 주면 글자가 화면에서 뒤집혀 보이지 않게 방향을 고른다.
Geometry build(const LotGameObject::Dim& dim, const mat4& transform, const LotCamera* camera);

// 오브젝트의 치수를 선/글자 시스템에 넣는다. color 는 선 색 (글자는 조금 밝게).
void draw(const LotGameObject& obj, const LotCamera& camera, LineRenderSystem& lines,
          TextRenderSystem& text, const vec3& color);

// 피킹/박스 선택/전체 보기용 점들 (월드): p1, p2, 치수선 양 끝.
std::vector<vec3> outlinePoints(const LotGameObject& obj);

// 값 문자열 (소수 자릿수 precision).
std::string formatValue(float value, int precision);

}  // namespace lot_dim
