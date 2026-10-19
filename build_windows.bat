@echo off
rem ======================================================================
rem  CodeCompare - one-click Windows build (Qt 6 + MinGW from the Qt installer)
rem
rem  Result: dist\CodeCompare.exe (+ Qt DLLs) - copy the whole dist folder
rem          anywhere and run CodeCompare.exe. No installation needed.
rem
rem  If Qt is not in C:\Qt, set these before running, for example:
rem     set QT_DIR=D:\Qt\6.8.3\mingw_64
rem     set MINGW_DIR=D:\Qt\Tools\mingw1310_64
rem ======================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
echo CodeCompare build
echo.

rem ---- find Qt (newest C:\Qt\6.x\mingw_64) ----
if not defined QT_DIR (
    for /f "delims=" %%V in ('dir /b /ad /o-n "C:\Qt\6.*" 2^>nul') do (
        if not defined QT_DIR if exist "C:\Qt\%%V\mingw_64\bin\qmake.exe" set "QT_DIR=C:\Qt\%%V\mingw_64"
    )
)
if not defined QT_DIR (
    echo [ERROR] Qt 6 for MinGW was not found in C:\Qt.
    echo         Run setup_qt.bat first ^(installs Qt automatically^), or set QT_DIR - see README.md.
    goto :fail
)

rem ---- find the MinGW compiler that ships with Qt ----
if not defined MINGW_DIR (
    for /f "delims=" %%M in ('dir /b /ad /o-n "C:\Qt\Tools\mingw*_64" 2^>nul') do (
        if not defined MINGW_DIR set "MINGW_DIR=C:\Qt\Tools\%%M"
    )
)
if not defined MINGW_DIR (
    echo [ERROR] MinGW was not found in C:\Qt\Tools.
    echo         Run setup_qt.bat first, or set MINGW_DIR - see README.md.
    goto :fail
)

rem Qt's own tools first so another MinGW/CMake on PATH (e.g. C:\mingw64) is not used.
set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"

echo Using Qt    : %QT_DIR%
echo Using MinGW : %MINGW_DIR%
echo.

where cmake >nul 2>nul || (echo [ERROR] CMake not found. Run setup_qt.bat first. & goto :fail)
where ninja >nul 2>nul || (echo [ERROR] Ninja not found. Run setup_qt.bat first. & goto :fail)

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%" || goto :fail
cmake --build build || goto :fail

echo.
echo ---- running unit tests ----
ctest --test-dir build --output-on-failure || goto :fail

echo.
echo ---- creating dist folder ----
if exist dist rmdir /s /q dist
mkdir dist
copy /y build\CodeCompare.exe dist\ >nul
copy /y build\codecompare-cli.exe dist\ >nul
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw dist\CodeCompare.exe || goto :fail

echo.
echo ======================================================================
echo  Done:  %CD%\dist\CodeCompare.exe
echo ======================================================================
start "" "%CD%\dist"
pause
exit /b 0

:fail
echo.
echo [FAILED] See the message above. Copy it to Claude if you need help.
pause
exit /b 1
