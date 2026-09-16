@echo off
REM Build script for Jockey Windows kernel driver
REM Requires Windows Driver Kit (WDK) and Visual Studio Build Tools

echo Building Jockey Windows Kernel Driver...
echo.

REM Check for WDK
where msbuild >nul 2>&1
if errorlevel 1 (
    echo ERROR: msbuild not found. Install Visual Studio Build Tools + WDK.
    pause
    exit /b 1
)

REM Build the driver
msbuild jockey.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal

if errorlevel 1 (
    echo.
    echo Build failed. Check errors above.
    pause
    exit /b 1
)

echo.
echo Build successful: x64\Release\jockey.sys
pause
