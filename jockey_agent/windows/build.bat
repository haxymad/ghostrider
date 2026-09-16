@echo off
setlocal enabledelayedexpansion
echo compiling userspace agent...

set CFLAGS=/nologo /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 ^
 /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS ^
 /Iinclude /Iplatform /Ikmod ^
 /FIstdlib.h /FIstring.h

if not exist obj mkdir obj

call :cc src\main.c                    || goto :fail
call :cc src\vm.c                      || goto :fail
call :cc src\value.c                   || goto :fail
call :cc src\loader.c                  || goto :fail
call :cc src\agent.c                   || goto :fail
call :cc src\agent_c2.c                || goto :fail
call :cc src\builtins.c                || goto :fail
call :cc src\kernel_bridge.c           || goto :fail
call :cc src\builtins\anti.c           || goto :fail
call :cc src\builtins\system.c         || goto :fail
call :cc src\builtins\fs.c             || goto :fail
call :cc src\builtins\process.c        || goto :fail
call :cc src\builtins\memory.c         || goto :fail
call :cc src\builtins\net.c            || goto :fail
call :cc src\builtins\cred.c           || goto :fail
call :cc src\builtins\kernel.c         || goto :fail
call :cc src\builtins\util.c           || goto :fail
call :cc src\builtins\pure.c           || goto :fail
call :cc src\builtins\crypto.c         || goto :fail
call :cc src\builtins\data.c           || goto :fail
call :cc src\platform\windows\platform.c || goto :fail

echo linking...
cl /nologo kernel32.lib advapi32.lib user32.lib ws2_32.lib iphlpapi.lib ^
   secur32.lib rpcrt4.lib psapi.lib netapi32.lib bcrypt.lib ^
   /Fejockey.exe obj\*.obj
if errorlevel 1 goto :fail

echo done.
endlocal
exit /b 0

:cc
cl %CFLAGS% /Foobj\ /c %~1
exit /b %errorlevel%

:fail
echo.
echo *** BUILD FAILED -- fix the errors above and re-run ***
endlocal
exit /b 1