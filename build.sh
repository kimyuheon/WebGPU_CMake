#!/bin/bash
echo "========================================"
echo "WebGPU Project Build Script"
echo "========================================"
echo ""

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# "dist" 를 붙이면 배포 빌드 (로그/디버그 정보 제거).
# 평소 개발에는 붙이지 않는다 - 로그가 있어야 초기화 단계를 따라갈 수 있다.
CMAKE_ARGS=""
if [ "$1" = "dist" ]; then
    CMAKE_ARGS="-DLOT_DIST=ON"
    echo "*** DIST BUILD - logging and debug info will be stripped ***"
    echo ""
else
    CMAKE_ARGS="-DLOT_DIST=OFF"
fi

# Step 1: Activate emsdk environment
echo "[1/3] Activating Emscripten..."
cd "$SCRIPT_DIR/../emsdk"
if [ ! -f "emsdk_env.sh" ]; then
    echo "ERROR: emsdk_env.sh not found!"
    exit 1
fi
source ./emsdk_env.sh

# Step 2: Go back to project
echo ""
echo "[2/3] Back to project directory..."
cd "$SCRIPT_DIR"

# Step 3: Configure and build
echo ""
echo "[3/3] Configuring and building..."
emcmake cmake -B build $CMAKE_ARGS
if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed!"
    exit 1
fi

cmake --build build
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed!"
    exit 1
fi

echo ""
echo "========================================"
echo "Build SUCCESS!"
echo "========================================"
echo ""
echo "Files: build/WebGPUApp.html"
echo "Run: cd build && python3 -m http.server 8000"
echo ""
