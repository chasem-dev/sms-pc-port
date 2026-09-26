@echo off
setlocal
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
  echo MSYS2 was not found at "%MSYS2_ROOT%". Set MSYS2_ROOT to its install folder. 1>&2
  exit /b 1
)
rem SMS_ARCH=64 uses MSYS2 MINGW64 (the 64-bit build), otherwise MINGW32.
set "SMS_ENV=MINGW32"
if "%SMS_ARCH%"=="64" set "SMS_ENV=MINGW64"
set "MSYSTEM=%SMS_ENV%"
set "CHERE_INVOKING=1"
set "PATH=%MSYS2_ROOT%\%SMS_ENV%\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
pushd "%~dp0"
"%MSYS2_ROOT%\usr\bin\bash.exe" ./build.sh %*
set "result=%ERRORLEVEL%"
popd
exit /b %result%
