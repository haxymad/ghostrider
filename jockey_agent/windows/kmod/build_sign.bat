@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM ===== Configuration =====
set "MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
set "SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe"
set "CERT=1F2DF6D273BBF5C4D5EC781BCA02BC6C4329310A"
set "SYS=x64\Release\jockey.sys"
set "SERVICE=jockey"

REM ===== Must be elevated =====
net session >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Run this script as Administrator.
    exit /b 1
)

REM ===== 1. Try to stop the driver =====
echo [1/5] Stopping driver "%SERVICE%"...
sc query "%SERVICE%" >nul 2>&1
if not errorlevel 1 (
    sc stop "%SERVICE%" >nul 2>&1
    if errorlevel 1 (
        echo       sc stop failed - driver may be NOT_STOPPABLE.
    ) else (
        echo       Stop requested. Waiting for STOPPED...
        for /l %%i in (1,1,10) do (
            sc query "%SERVICE%" | findstr /i "STOPPED" >nul && goto :stopped
            timeout /t 1 >nul
        )
        :stopped
        sc query "%SERVICE%" | findstr /i "STOPPED" >nul && echo       STOPPED. || echo       Still RUNNING - file will be locked.
    )
) else (
    echo       Service not registered - skipping stop.
)

REM ===== 2. Build =====
echo [2/5] Building...
"%MSBUILD%" jockey.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:EnableInf2cat=false /t:Rebuild /v:n
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)
if not exist "%SYS%" (
    echo [ERROR] "%SYS%" not found after build.
    exit /b 1
)

REM ===== 3. Sign =====
echo [3/5] Signing "%SYS%"...
"%SIGNTOOL%" sign /v /sm /s My /sha1 %CERT% /fd sha256 /a "%SYS%"
if errorlevel 1 (
    echo [ERROR] Signing failed. Is the driver still loaded?
    exit /b 1
)

REM ===== 4. Verify =====
echo [4/5] Verifying signature...
"%SIGNTOOL%" verify /v /pa "%SYS%"
if errorlevel 1 (
    echo [ERROR] Signature verification failed.
    exit /b 1
)

REM ===== 5. Start =====
echo [5/5] Starting driver "%SERVICE%"...
sc start "%SERVICE%"
sc query "%SERVICE%"

echo.
echo Done.
endlocal
exit /b 0