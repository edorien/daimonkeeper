@echo off
setlocal

REM ===========================================================================
REM  build-package-windows.bat
REM
REM  Native Windows MinGW build + full package: keeperfx.exe, keeperfx_hvlog.exe,
REM  the SDL3 runtime DLLs and all game data (configs, campaigns, levels,
REM  language / sound .dat files). Produces:
REM
REM    pkg\keeperfx*.7z     - the release archive (CPack)
REM    dist\windows\        - the same contents unpacked, ready to run
REM
REM  The Linux build and the coverage / unit-test pass are intentionally
REM  excluded.
REM
REM  This is a thin wrapper: it boots the MSYS2 "MINGW32" environment and runs
REM  build-package-windows.sh inside it. See that script's header for the full
REM  requirements. In short, install MSYS2 (https://www.msys2.org) and:
REM
REM    pacman -S --needed git make p7zip curl unzip ^
REM      mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
REM
REM  First run needs network access (fetches third-party libs, clones
REM  dkfans/FXGraphics, downloads the pngpal2raw tool).
REM
REM  Usage - from a normal Command Prompt in the repo root:
REM
REM    build-package-windows.bat
REM
REM  Optional environment variables:
REM
REM    set MSYS2_ROOT=D:\msys64      if MSYS2 is not installed at C:\msys64
REM    set BUILD_NUMBER=1234         override the build number (default: commit count)
REM    set PACKAGE_SUFFIX=Alpha      label appended to the archive name
REM ===========================================================================

if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "MSYS2_SHELL=%MSYS2_ROOT%\msys2_shell.cmd"

if not exist "%MSYS2_SHELL%" (
    echo error: "%MSYS2_SHELL%" not found.
    echo        Install MSYS2 from https://www.msys2.org, or point MSYS2_ROOT
    echo        at your MSYS2 install directory ^(set MSYS2_ROOT=...^).
    exit /b 1
)

cd /d "%~dp0"

REM -mingw32   : MSYSTEM=MINGW32 (32-bit toolchain, matches the prebuilt deps)
REM -defterm   : don't open a new terminal window
REM -no-start  : run in this console and wait for it to finish
REM -here      : keep the current directory (the repo root)
REM -c         : the command to run
call "%MSYS2_SHELL%" -mingw32 -defterm -no-start -here -c "./build-package-windows.sh"
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo BUILD OK
) else (
    echo BUILD FAILED ^(exit %RC%^)
)
exit /b %RC%
