#pragma once

#include "lot_math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 해치(HATCH) - 닫힌 경계 안을 무늬(빗금 · 점)나 실선으로 채운다. GPU 없음(순수 기하).
// 네이티브 lot_hatch.{h,cpp} 를 그대로 옮긴 것이다 (glm 대신 여기 2D 벡터).
//
// 정의는 AutoCAD 와 같다:
//   - 경계 = 평면 위 닫힌 루프 여러 개(2D, 해치 평면 좌표). 홀수/짝수 규칙으로 안팎을 정한다 - 겹치면 구멍.
//   - 무늬 = 선 가족 목록. 가족마다 각도 · 기준점 · 다음 선까지의 오프셋 · 대시(양수 선, 음수 빈칸, 0 점).
//     값은 이미 축척 · 회전이 적용된 것 (DXF HATCH 의 53/43/44/45/46/49 와 같은 뜻).
//   - solid = 무늬 대신 삼각형으로 채움.
// 내장 무늬(ANSI31 ...)는 acad.pat 값(인치 x 25.4 = mm)이고 scale · angle 로 돌려 쓴다.
namespace lot_hatch {

struct P2 {
    float x = 0.0f, y = 0.0f;
};
inline P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
inline P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
inline P2 operator*(P2 a, float s) { return {a.x * s, a.y * s}; }

struct PatternLine {
    float angleDeg = 0.0f;   // 선 방향 (해치 평면 X 축 기준)
    P2 base{0.0f, 0.0f};     // 기준점
    P2 offset{0.0f, 1.0f};   // 다음 선까지 (평면 좌표 - 이미 회전됨)
    std::vector<float> dashes;   // 비면 실선
};

struct HatchData {
    // 평면(로컬): origin + x*right + y*up. right/up 은 단위 벡터.
    vec3 origin{0.0f, 0.0f, 0.0f}, right{1.0f, 0.0f, 0.0f}, up{0.0f, 1.0f, 0.0f};
    std::vector<std::vector<P2>> loops;   // 닫힌 경계들 (평면 좌표)
    bool solid = false;
    std::string patternName = "ANSI31";
    float angleDeg = 0.0f;
    float scale = 1.0f;
    std::vector<PatternLine> lines;       // 실제 무늬 (solid 면 무시)
};

// 무늬 선분 - 경계 안쪽만, 대시 적용. (a, b) 쌍 (평면 좌표). maxSegments 로 폭주 방지.
std::vector<std::pair<P2, P2>> generateSegments(const HatchData& h, std::size_t maxSegments = 200000);

// 실선 채움 삼각화 (even-odd: 바깥 / 구멍 깊이).
struct Fill {
    std::vector<P2> points;
    std::vector<uint32_t> indices;
};
Fill triangulate(const HatchData& h);

// 내장 무늬 이름 -> 선 가족 (mm, 각도 0, 축척 1). 모르면 false.
bool builtinPattern(const std::string& name, std::vector<PatternLine>& out);
std::vector<PatternLine> transformPattern(const std::vector<PatternLine>& def, float scale, float angleDeg);

bool contains(const std::vector<std::vector<P2>>& loops, const P2& p);
float loopArea(const std::vector<P2>& loop);

}  // namespace lot_hatch

class LotGameObject;
namespace lot_hatch {
// 객체에 해치를 붙인다 (네이티브 setupHatchObject): 정의 + 바깥 경계 점(피킹 · 범위, 로컬) + 무늬 선분.
// h 의 평면은 객체 로컬 좌표다. 단색 채움 모델은 그리는 쪽이 처음 그릴 때 만든다.
void attach(LotGameObject& obj, std::shared_ptr<const HatchData> h);
}  // namespace lot_hatch
