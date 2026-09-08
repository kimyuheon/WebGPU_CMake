#pragma once

#include "lot_model.h"

#include <string>

// Wavefront OBJ 파서.
//
// 전체 규격을 다 지원하지는 않는다. 메시 하나를 읽는 데 필요한 만큼만 본다:
//   v  x y z [r g b]   위치 (색은 표준이 아닌 확장이지만 흔하다)
//   vn x y z           법선
//   vt u v             텍스처 좌표 - 아직 정점에 넣지 않으므로 읽고 버린다
//   f  a/b/c ...       면. 사각형 이상은 삼각형 팬으로 쪼갠다
// mtllib/usemtl/o/g/s 같은 줄은 무시한다.
namespace lot_obj {

// 파싱 결과. 실패해도 예외를 던지지 않는다 - 비동기 로딩 콜백 안에서
// 부르는 경우가 많아 호출부가 확인하는 편이 낫다.
struct LoadResult {
    bool ok = false;
    std::string error;
    LotModel::Builder builder;
};

// OBJ 파일 '내용'을 파싱한다 (경로가 아니다).
// 웹에서는 파일을 비동기로 받아오므로, 받아온 뒤 이 함수에 넘긴다.
LoadResult parse(const std::string& text);

}  // namespace lot_obj
