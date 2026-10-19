@echo off
rem ======================================================================
rem  CodeCompare - install Qt 6 + MinGW + CMake + Ninja into C:\Qt
rem  (no Qt account needed; uses the open-source "aqtinstall" tool)
rem
rem  Needs: Python 3 for Windows and an internet connection.
rem  Download size is about 1 GB. Run once, then run build_windows.bat.
rem ======================================================================
setlocal
set "QT_VERSION=6.8.3"
set "QT_ROOT=C:\Qt"

rem ---- find Python ----
set "PY="
where python >nul 2>nul && set "PY=python"
if not defined PY where py >nul 2>nul && set "PY=py -3"
if not defined PY (
    echo [ERROR] Python was not found.
    echo         Install Python 3 from https://www.python.org/downloads/
    echo         and tick "Add python.exe to PATH" in the installer. Then run this again.
    goto :fail
)
echo Using Python: %PY%
%PY% --version || goto :fail

echo.
echo ---- installing aqtinstall ----
%PY% -m pip install --upgrade aqtinstall || goto :fail

echo.
echo ---- installing Qt %QT_VERSION% for MinGW 64-bit into %QT_ROOT% ----
%PY% -m aqt install-qt windows desktop %QT_VERSION% win64_mingw --outputdir "%QT_ROOT%" || goto :fail

echo.
echo ---- installing MinGW 13.1 compiler ----
%PY% -m aqt install-tool windows desktop tools_mingw1310 --outputdir "%QT_ROOT%" || goto :fail

echo.
echo ---- installing CMake ----
%PY% -m aqt install-tool windows desktop tools_cmake --outputdir "%QT_ROOT%" || goto :fail

echo.
echo ---- installing Ninja ----
%PY% -m aqt install-tool windows desktop tools_ninja --outputdir "%QT_ROOT%" || goto :fail

echo.
echo ======================================================================
echo  Qt is installed in %QT_ROOT%.
echo  Now double-click build_windows.bat to build CodeCompare.exe
echo ======================================================================
pause
exit /b 0

:fail
echo.
echo [FAILED] See the message above.
pause
exit /b 1
