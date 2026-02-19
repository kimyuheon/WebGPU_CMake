cmake_minimum_required(VERSION 3.10)
project(3DEngineWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Emscripten 컴파일러 플래그
set(CMAKE_EXECUTABLE_SUFFIX ".html")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sWASM=1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -sALLOW_MEMORY_GROWTH=1")

# Emscripten 링커 플래그
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_RUNTIME_METHODS=HEAPF32,UTF8ToString")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sEXPORTED_FUNCTIONS=_main,_onWindowResize")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/js/webgpu_bindings.js")

# 소스 파일
file(GLOB SOURCE_FILES "src/*.cpp")

add_executable(WebGPUApp
    ${SOURCE_FILES}
)

# 빌드 폴더에 shaders 디렉토리 생성 및 복사
add_custom_command(TARGET WebGPUApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WebGPUApp>/shaders"
    COMMENT "Creating shaders directory in build folder"
)

# 소스에 셰이더 폴더가 있으면 복사
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    add_custom_command(TARGET WebGPUApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:WebGPUApp>/shaders"
        COMMENT "Copying shaders to build directory"
    )
    message(STATUS "Shaders will be copied from: ${CMAKE_CURRENT_SOURCE_DIR}/shaders")
else()
    message(STATUS "Shaders folder will be created empty. Add shaders to: ${CMAKE_CURRENT_SOURCE_DIR}/shaders")
endif()

message(STATUS "JavaScript library: ${CMAKE_CURRENT_SOURCE_DIR}/src/js/webgpu_bindings.js")
