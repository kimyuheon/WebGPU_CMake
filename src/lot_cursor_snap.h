#pragma once

#include "lot_math.h"
#include "lot_sketch_tool.h"  // SketchPlane

#include <string>

// 커서 보정 - 도구들이 화면 점을 월드 점으로 바꾼 뒤 공통으로 거치는 단계.
//
// 우선순위는 AutoCAD 와 같다:
//   1. 오브젝트 스냅(osnap) 이 잡혔으면 그 점 - 다른 보정은 하지 않는다.
//   2. 직교 트랙킹(F8) 이 켜져 있고 기준점이 있으면 한 축으로만.
//   3. 그리드 스냅(F9) 이 켜져 있으면 격자 눈금으로.
// 스케치 도구와 변환 도구가 같은 규칙을 쓰도록 여기 한 곳에 둔다.
namespace lot_cursor {

struct Settings {
    bool ortho = false;       // F8
    float gridSpacing = 0.0f; // F9. 0 이면 끔 (월드 단위)
};

// 전역 설정 - main 이 키 입력으로 바꾸고 도구들이 읽는다. 도구마다 복사본을 들면
// 둘이 어긋나므로 (F8 은 스케치에만 걸리고 변환에는 안 걸리는 식) 한 벌만 둔다.
Settings& settings();

// 평면 위 점을 보정한다. reference 는 직교 트랙킹의 기준점 (없으면 nullptr).
// osnap 이 잡힌 점에는 부르지 않는다 (호출자가 먼저 걸러낸다).
vec3 apply(const vec3& point, const SketchPlane& plane, const vec3* reference);

// 안내문 꼬리 (" ORTHO SNAP 0.5"). 켜진 게 없으면 빈 문자열.
std::string statusSuffix();

// '보기 좋은' 격자 간격 (1, 2, 5 × 10^k) 중 target 에 가장 가까운 것.
// 씬 크기에 맞춰 간격을 정할 때 쓴다 - mm 도면에서 0.5 는 쓸모가 없다.
float niceSpacing(float target);

}  // namespace lot_cursor
