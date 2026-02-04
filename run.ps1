Write-Host "========================================"
Write-Host "WebGPU App Runner"
Write-Host "========================================"
Write-Host ""

# 빌드 폴더 확인
$htmlFile = Join-Path $PSScriptRoot "build\WebGPUApp.html"
if (-not (Test-Path $htmlFile)) {
    Write-Host "ERROR: WebGPUApp.html not found!" -ForegroundColor Red
    Write-Host "Please run build.ps1 first."
    pause
    exit 1
}

# build 폴더로 이동
$buildPath = Join-Path $PSScriptRoot "build"
Set-Location $buildPath

Write-Host "Starting local web server..."
Write-Host ""
Write-Host "Server will run at: http://localhost:8000"
Write-Host "Open this URL in your browser: http://localhost:8000/WebGPUApp.html"
Write-Host ""
Write-Host "Press Ctrl+C to stop the server."
Write-Host "========================================"
Write-Host ""

# Python 웹서버 실행
python -m http.server 8000
