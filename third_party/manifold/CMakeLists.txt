# Manifold (메시 불리언) - 네이티브 3dEngine/external/manifold 와 같은 판의 src/include 만 옮겨 왔다.
# 원래 CMake 는 설치 · 바인딩 · 테스트까지 끌고 와서, 웹에 필요한 정적 라이브러리만 여기서 만든다.
# 옵션은 네이티브와 같다: 병렬(TBB) 끔, 예외는 MANIFOLD_DEBUG 일 때만 (꺼 둔다).
file(GLOB MANIFOLD_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(manifold STATIC ${MANIFOLD_SRCS})
target_include_directories(manifold PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(manifold PUBLIC cxx_std_17)
target_compile_definitions(manifold PUBLIC MANIFOLD_PAR=-1)
if(LOT_DIST)
    target_compile_definitions(manifold PUBLIC MANIFOLD_NO_IOSTREAM MANIFOLD_NO_FILESYSTEM)
endif()
# 원본과 같이: FMA 같은 부동소수 최적화가 결과를 바꾸지 않게 (불리언이 판정을 뒤집는다)
target_compile_options(manifold PRIVATE -O3 -ffp-contract=off -w)
