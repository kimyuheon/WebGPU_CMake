#pragma once

#include "lot_game_object.h"
#include "lot_layers.h"

#include <string>

// DXF 읽기 (AutoCAD Drawing Exchange Format, ASCII).
//
// 도면 하나가 선·원·호·폴리선·문자로 되어 있으면 이 엔진의 스케치 오브젝트와
// 거의 1:1 이라 그대로 옮길 수 있다. 층은 TABLES 의 LAYER 항목에서 만들고
// (이름/색/선종류/꺼짐/잠김), 엔티티의 layer 이름으로 이어 붙인다.
//
// 다루는 엔티티: LINE, CIRCLE, ARC, LWPOLYLINE, POLYLINE+VERTEX, TEXT, SPLINE(근사),
// SOLID(테두리). 나머지(INSERT/블록, HATCH, DIMENSION, MTEXT …)는 세고 건너뛴다.
//
// 글자 인코딩은 여기서 다루지 않는다 - 옛 DXF 는 CP949 같은 코드페이지를 쓰므로
// JS 쪽에서 TextDecoder 로 UTF-8 로 바꿔 넘긴다 (src/js/lot_toolbar.js).
namespace lot_dxf {

struct LoadStats {
    int lines = 0;
    int circles = 0;
    int arcs = 0;
    int polylines = 0;
    int texts = 0;
    int splines = 0;      // 제어점을 이어 근사한 것
    int skipped = 0;      // 지원하지 않는 엔티티
    std::string skippedKinds;
    int layers = 0;
    // 원점에서 먼 도면은 이만큼 빼고 읽는다 (float 정밀도). 다시 쓸 때 더한다.
    double originX = 0.0;
    double originY = 0.0;
    std::string error;    // 비어 있지 않으면 실패
};

// 텍스트를 읽어 objects / layers 에 채운다 (둘 다 비우고 시작한다).
LoadStats load(const std::string& text, LotGameObject::Map& objects, LotLayers& layers);

// ---- 쓰기 ----
//
// R12 (AC1009) ASCII 로 쓴다. 가장 오래된 규격이라 핸들도 서브클래스 표시도 필요 없고,
// 어떤 CAD 든 받아 준다. 그래서 LWPOLYLINE 대신 POLYLINE+VERTEX+SEQEND 를 쓴다.
//
// 2D 로 낼 수 있는 것만 나간다: 선 · 원 · 호 · 폴리선 · 문자. 치수는 DIMENSION 엔티티가
// 블록을 달고 다녀야 해서, 보이는 대로 선과 문자로 풀어 쓴다 (연관성은 잃는다).
// 메시(큐브·OBJ)는 2D 도면 엔티티가 아니므로 건너뛴다 - 그건 .lot 으로 저장한다.
//
// 읽을 때 bulge (호가 섞인 폴리선) 를 점으로 잘라 두므로, 다시 쓰면 그 자리는
// 곧은 선분들로 나간다 - 모양은 같지만 bulge 값은 돌아오지 않는다.
//
// 색은 ACI 번호 하나로 나간다 (R12 에는 트루컬러가 없다). DXF 에서 읽어 온 색은
// 그 번호로 정확히 돌아오지만, 색판에서 고른 임의의 색은 가장 가까운 번호로 맞춘다.
struct SaveStats {
    int lines = 0;
    int circles = 0;
    int arcs = 0;
    int polylines = 0;
    int texts = 0;
    int dimensions = 0;   // 선/문자로 풀어 쓴 것
    int skipped = 0;      // 메시 등 2D 로 낼 수 없는 것
    int layers = 0;
};

// linetypeScale 은 헤더의 $LTSCALE 로 나간다.
std::string save(const LotGameObject::Map& objects, const LotLayers& layers,
                 float linetypeScale = 1.0f, SaveStats* stats = nullptr,
                 double originX = 0.0, double originY = 0.0);

}  // namespace lot_dxf
