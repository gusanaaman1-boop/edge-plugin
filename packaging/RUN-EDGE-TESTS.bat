@echo off
setlocal enabledelayedexpansion
title EDGE - measurement suites

REM ---------------------------------------------------------------------------
REM  OPTIONAL. Installing does not depend on this - it exists so the same
REM  numbers measured on the Mac can be measured on YOUR machine, with your
REM  compiler and your floating-point behaviour.
REM
REM  Run INSTALL-EDGE.bat first: this reuses that build.
REM ---------------------------------------------------------------------------

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
if not exist "%ROOT%\CMakeLists.txt" (
    if exist "%ROOT%\..\CMakeLists.txt" (
        pushd "%ROOT%\.."
        set "ROOT=!CD!"
        popd
    )
)

set "BUILD=%ROOT%\build-win"
set "LOG=%ROOT%\EDGE-test-log.txt"

cls
echo.
echo   ============================================================
echo     EDGE - measurement suites
echo   ============================================================
echo.

if not exist "%BUILD%\CMakeCache.txt" (
    echo   [X] No build here yet. Run INSTALL-EDGE.bat first.
    echo.
    pause
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    for %%P in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    ) do (
        if exist "%%~P\cmake.exe" set "PATH=%%~P;!PATH!"
    )
)

echo   Building the two suites...
cmake --build "%BUILD%" --config Release --target EdgeTests EdgeHostTests --parallel > "%LOG%" 2>&1
if errorlevel 1 (
    echo   [X] The suites did not build. See:
    echo       %LOG%
    echo.
    pause
    exit /b 1
)

REM  Multi-config generators add a Release\ level, single-config ones do not.
set "T1=%BUILD%\EdgeTests_artefacts\Release\EdgeTests.exe"
if not exist "!T1!" set "T1=%BUILD%\EdgeTests_artefacts\EdgeTests.exe"
set "T2=%BUILD%\EdgeHostTests_artefacts\Release\EdgeHostTests.exe"
if not exist "!T2!" set "T2=%BUILD%\EdgeHostTests_artefacts\EdgeHostTests.exe"

if not exist "!T1!" (
    echo   [X] EdgeTests.exe was not produced. See:
    echo       %LOG%
    echo.
    pause
    exit /b 1
)

echo.
echo   ---------------- DSP measurements ----------------
"!T1!"
set "R1=!ERRORLEVEL!"

echo.
echo   ---------------- host contract -------------------
"!T2!"
set "R2=!ERRORLEVEL!"

echo.
if "!R1!"=="0" if "!R2!"=="0" (
    echo   ============================================================
    echo     Every check passed on this machine.
    echo   ============================================================
    echo.
    pause
    exit /b 0
)

echo   ============================================================
echo     Some checks FAILED on this machine.
echo     Every check prints the value it measured - the failing
echo     lines above say which number is wrong. Send me a screenshot.
echo   ============================================================
echo.
pause
exit /b 1
