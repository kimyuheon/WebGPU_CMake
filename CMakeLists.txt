cmake_minimum_required(VERSION 3.13)
project(3DEngineWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ------------------------------------------------------------------
# 배포 빌드
#
# 평소 개발에는 끈다 (로그가 있어야 초기화 단계를 따라갈 수 있다).
# 켜면 배포물에서 내부 구조가 드러나는 것들을 걷어낸다:
#   - 로그 매크로가 통째로 사라진다. 출력만 막으면 문자열 리터럴이
#     wasm 에 남아 strings 만 돌려도 클래스 이름이 읽힌다.
#   - iostream 도 같이 빠진다 (스트림 초기화 + 로케일 문자열 덩어리).
#   - assert 메시지 문자열 제거, 디버그 정보 제거.
#
# 다만 wasm 은 디컴파일이 가능하다. 이름을 지워도 로직 구조는 복원되므로
# "안 보이게" 가 아니라 "귀찮게" 까지가 한계다.
#   ./build.sh dist   (Windows: build.bat dist)
# ------------------------------------------------------------------
option(LOT_DIST "Strip logging and debug info for distribution" OFF)

set(CMAKE_EXECUTABLE_SUFFIX ".html")

# ------------------------------------------------------------------
# emdawnwebgpu 포트 (Dawn 의 webgpu.h 구현)
#
# 컴파일과 링크 양쪽에 모두 전달해야 한다.
# cpp_bindings=false : webgpu_cpp.h 는 Dawn 이 "안정성을 보장하지 않는다"고
#                      명시한 헤더이므로 include 경로에서 아예 제외한다.
#                      우리는 C 헤더(webgpu.h)만 쓴다.
#
# 주의: emsdk 버전을 올리면 이 포트에 핀 고정된 Dawn 버전도 함께 올라간다.
#       올릴 때는 의도적으로, 한 번에 올리고 빌드를 확인할 것.
# ------------------------------------------------------------------
set(EMDAWNWEBGPU_PORT "--use-port=emdawnwebgpu:cpp_bindings=false")
add_compile_options(${EMDAWNWEBGPU_PORT})
add_link_options(${EMDAWNWEBGPU_PORT})

# Emscripten 링커 플래그
add_link_options(
    -sALLOW_MEMORY_GROWTH=1
    --js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/js/webgpu_bindings.js
)

if(LOT_DIST)
    # 로그 매크로를 빈 것으로 만든다 (src/lot_log.h 참고)
    add_compile_definitions(LOT_LOGGING=0)
    # -fno-rtti: RTTI 를 끄면 typeinfo 에 박히는 맹글링된 클래스 이름이 사라진다
    # (LotModel, shared_ptr<LotModel> 같은 것들). dynamic_cast/typeid 를 쓰지
    # 않으므로 잃는 것이 없다.
    add_compile_options(-O3 -g0 -fno-rtti)
    add_link_options(-O3 -g0 -sASSERTIONS=0)
    message(STATUS "LOT_DIST: logging removed, debug info stripped")
else()
    add_link_options(-sASSERTIONS=1)
endif()

# 릴리스 빌드에서는 Closure 로 JS 글루 코드를 줄인다 (Dawn README 권장).
add_link_options($<$<CONFIG:Release>:--closure=1>)

# 소스 파일
file(GLOB SOURCE_FILES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(WebGPUApp ${SOURCE_FILES})

# JS 라이브러리가 바뀌면 다시 링크되도록 의존성을 걸어둔다.
set_target_properties(WebGPUApp PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/js/webgpu_bindings.js
)

# 셰이더를 빌드 폴더로 복사
add_custom_command(TARGET WebGPUApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WebGPUApp>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:WebGPUApp>/shaders"
    COMMENT "Copying shaders to build directory"
)

# 모델도 같이 복사한다. 셰이더와 마찬가지로 런타임에 fetch 로 받아온다.
add_custom_command(TARGET WebGPUApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WebGPUApp>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:WebGPUApp>/models"
    COMMENT "Copying models to build directory"
)
