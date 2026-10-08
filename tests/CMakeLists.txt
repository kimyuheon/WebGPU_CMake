# 기하 단위 테스트 (GPU · 브라우저 없음) - Node 로 돈다. 앱(WebGPUApp)의 링크 옵션
# (JS 라이브러리 · Closure) 과 섞이지 않게 따로 된 프로젝트다. 실행: tools/brep_tests.sh
cmake_minimum_required(VERSION 3.13)
project(3DEngineWebTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_subdirectory("${ROOT}/third_party/manifold" manifold)

# lot_model.h 가 webgpu.h 를 들이므로 헤더만 쓰는 포트를 켠다 (GPU 를 부르지는 않는다)
set(PORT "--use-port=emdawnwebgpu:cpp_bindings=false")

add_executable(brep_tests brep_tests.cpp "${ROOT}/src/lot_brep_shape.cpp" "${ROOT}/src/lot_brep_tessellator.cpp")
target_include_directories(brep_tests PRIVATE "${ROOT}/src")
target_compile_options(brep_tests PRIVATE ${PORT})
target_link_options(brep_tests PRIVATE ${PORT} -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1)
target_link_libraries(brep_tests PRIVATE manifold)
