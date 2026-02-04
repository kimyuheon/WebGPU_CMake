@echo off
echo ========================================
echo Clean Build Directory
echo ========================================
echo.

if exist "build" (
    echo Deleting build directory...
    rmdir /s /q build
    echo Done!
) else (
    echo Build directory does not exist.
)

echo.
pause
