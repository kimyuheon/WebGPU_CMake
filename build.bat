@echo off
echo ========================================
echo WebGPU Project Build Script
echo ========================================
echo.

REM Step 1: Add Ninja to PATH if available
if exist "C:\ninja\ninja.exe" (
    set "PATH=%PATH%;C:\ninja"
    echo Found Ninja at C:\ninja
)

REM Step 2: Activate emsdk environment
echo [1/4] Activating Emscripten...
cd /d "%~dp0\..\emsdk"
if not exist "emsdk_env.bat" (
    echo ERROR: emsdk_env.bat not found!
    pause
    exit /b 1
)
call emsdk_env.bat

REM Step 3: Go back to project
echo.
echo [2/4] Back to project directory...
cd /d "%~dp0"

REM Step 3: Configure with CMake
echo.
echo [3/4] Configuring with CMake...
call emcmake cmake -B build
if errorlevel 1 (
    echo ERROR: CMake failed!
    pause
    exit /b 1
)

REM Step 4: Build
echo.
echo [4/4] Building...
cmake --build build
if errorlevel 1 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build SUCCESS!
echo ========================================
echo.
echo Files: build\WebGPUApp.html
echo Run: cd build ^&^& python -m http.server 8000
echo.
pause
