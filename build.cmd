@echo off
setlocal
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
  echo MSYS2 was not found at "%MSYS2_ROOT%". Set MSYS2_ROOT to its install folder. 1>&2
  exit /b 1
)
set "MSYSTEM=MINGW32"
set "CHERE_INVOKING=1"
set "PATH=%MSYS2_ROOT%\mingw32\bin;%MSYS2_ROOT%\usr\bin;%PATH%"
pushd "%~dp0"
"%MSYS2_ROOT%\usr\bin\bash.exe" ./build.sh %*
set "result=%ERRORLEVEL%"
popd
exit /b %result%
