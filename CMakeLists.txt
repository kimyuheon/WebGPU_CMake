cmake_minimum_required(VERSION 3.13)
project(3DEngineWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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
    -sASSERTIONS=1
    --js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/js/webgpu_bindings.js
)

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
