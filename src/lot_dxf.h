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
    std::string error;    // 비어 있지 않으면 실패
};

// 텍스트를 읽어 objects / layers 에 채운다 (둘 다 비우고 시작한다).
LoadStats load(const std::string& text, LotGameObject::Map& objects, LotLayers& layers);

}  // namespace lot_dxf
