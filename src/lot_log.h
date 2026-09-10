#pragma once

// 로그 매크로.
//
// 배포 빌드(-DLOT_DIST=ON)에서는 매크로가 통째로 사라진다. 단순히 출력을
// 끄는 것으로는 부족하다 - 문자열 리터럴이 wasm 안에 그대로 남아서
// strings 만 돌려도 클래스 이름과 내부 구조가 읽히기 때문이다.
//
// <iostream> 도 여기서만 include 한다. iostream 은 그 자체로 덩치가 큰
// (스트림 초기화, 로케일 문자열) 물건이라 배포 빌드에서는 아예 빼는 편이 낫다.
//
// 쓰는 법 - 끝의 std::endl 은 매크로가 붙인다:
//     LOT_LOG("model loaded: " << name << " (" << count << " verts)");
//     LOT_ERR("failed to open " << path);
//
// 주의: 꺼진 빌드에서는 인자가 컴파일되지 않으므로 타입 검사도 안 된다.
// 로그 안에서만 쓰는 변수가 있으면 켠 빌드로도 한 번은 돌려볼 것.

#ifndef LOT_LOGGING
#define LOT_LOGGING 1
#endif

#if LOT_LOGGING

#include <iostream>

#define LOT_LOG(expr) do { std::cout << expr << std::endl; } while (0)
#define LOT_ERR(expr) do { std::cerr << expr << std::endl; } while (0)

#else

#define LOT_LOG(expr) do { } while (0)
#define LOT_ERR(expr) do { } while (0)

#endif
