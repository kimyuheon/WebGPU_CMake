@echo off
echo ========================================
echo WebGPU App Runner
echo ========================================
echo.

if not exist "build\WebGPUApp.html" (
    echo ERROR: WebGPUApp.html not found!
    echo Please run build.bat first.
    pause
    exit /b 1
)

cd /d "%~dp0\build"

echo Starting web server at http://localhost:8000
echo Open: http://localhost:8000/WebGPUApp.html
echo Press Ctrl+C to stop
echo ========================================
echo.

"%LOCALAPPDATA%\Python\bin\python.exe" -m http.server 8000
