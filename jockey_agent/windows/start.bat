@echo off
chcp 65001 >nul
echo ============================================================
echo   Jockey Windows — One-Click Start
echo ============================================================
echo.

set SCRIPT_DIR=%~dp0
set WIN_SRV=%SCRIPT_DIR%windows_server.py
set AGENT=%SCRIPT_DIR%jockey.exe
set DRIVER_DIR=%SCRIPT_DIR%kmod

echo [1/5] Building kernel driver...
pushd %DRIVER_DIR%
msbuild jockey.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:quiet 2>nul
if errorlevel 1 (
    echo [ERROR] Driver build failed
    popd
    pause
    exit /b 1
)
popd
echo       done.

echo [2/5] Stopping old driver instance (if any)...
sc stop jockey >nul 2>&1
timeout /t 2 /nobreak >nul

echo [3/5] Installing and starting driver...
sc create jockey binPath= "%DRIVER_DIR%\x64\Release\jockey\jockey.sys" type= kernel >nul 2>&1
if errorlevel 1 (
    echo [ERROR] sc create failed — try as Administrator
    pause
    exit /b 1
)
sc start jockey >nul 2>&1
if errorlevel 1 (
    echo [ERROR] sc start failed — try as Administrator
    pause
    exit /b 1
)
echo       driver loaded.

echo [4/5] Starting Windows Server (port 9090)...
start "JockeyWS" /B python "%WIN_SRV%" 2>nul
timeout /t 3 /nobreak >nul
echo       server started.

echo [5/5] Starting C2 Agent...
echo.
echo ============================================================
echo   All systems up.
echo   C2 Server:    http://localhost:8080
echo   Win Server:   http://localhost:9090
echo   Agent mode:   C2 client
echo.
echo   Press Ctrl+C here to stop the agent.
echo   Close this window to stop everything.
echo ============================================================
echo.

"%AGENT%" --c2 --c2-url http://127.0.0.1:8080

echo.
echo [agent stopped]
pause
