Write-Host "========================================"
Write-Host "WebGPU Project Build Script"
Write-Host "========================================"
Write-Host ""

# 1. emsdk 환경 활성화
Write-Host "[1/4] Activating Emscripten environment..."
$emsdkPath = Join-Path $PSScriptRoot "..\emsdk"
$emsdkEnv = Join-Path $emsdkPath "emsdk_env.ps1"

if (-not (Test-Path $emsdkEnv)) {
    Write-Host "ERROR: emsdk_env.ps1 not found!" -ForegroundColor Red
    Write-Host "Please install emsdk first."
    pause
    exit 1
}

Set-Location $emsdkPath
& .\emsdk_env.ps1

# 2. 프로젝트 폴더로 이동
Write-Host ""
Write-Host "[2/4] Navigating to project directory..."
Set-Location $PSScriptRoot

# 3. CMake 설정
Write-Host ""
Write-Host "[3/4] Configuring CMake with Emscripten..."
& emcmake cmake -B build
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed!" -ForegroundColor Red
    pause
    exit 1
}

# 4. 빌드
Write-Host ""
Write-Host "[4/4] Building project..."
cmake --build build
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed!" -ForegroundColor Red
    pause
    exit 1
}

Write-Host ""
Write-Host "========================================"
Write-Host "Build completed successfully!" -ForegroundColor Green
Write-Host "========================================"
Write-Host ""
Write-Host "Output files:"
Write-Host "  - build\WebGPUApp.html"
Write-Host "  - build\WebGPUApp.js"
Write-Host "  - build\WebGPUApp.wasm"
Write-Host ""
Write-Host "To run: cd build; python -m http.server 8000"
Write-Host "Then open: http://localhost:8000/WebGPUApp.html"
Write-Host ""
pause
