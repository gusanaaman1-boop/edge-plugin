@echo off
setlocal enabledelayedexpansion
title EDGE - Make a Windows installer

REM ---------------------------------------------------------------------------
REM  Turns this source into ONE file you can send to anybody:
REM
REM      dist\EDGE-<version>-windows.exe
REM
REM  The person receiving it needs nothing at all - no Visual Studio, no CMake,
REM  no git. They double-click it and EDGE is installed.
REM
REM  You only need this if you want to GIVE EDGE to someone. To install it on
REM  THIS machine, INSTALL-EDGE.bat is the whole story.
REM
REM  This script does not need administrator rights: it builds and packs, it
REM  does not install. Refusing elevation it does not need is the honest thing
REM  to do.
REM
REM  No text echoed by this script may contain the characters greater-than,
REM  less-than, ampersand, pipe or caret - inside an echo Windows treats them as
REM  operators and silently mangles the output.
REM ---------------------------------------------------------------------------

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM  This file ships at the bundle root AND in packaging\. Walk up if the source
REM  is the parent's, so the build lands where INSTALL-EDGE.bat also puts it and
REM  the two share their object files instead of each paying for a full build.
if not exist "%ROOT%\CMakeLists.txt" (
    if exist "%ROOT%\..\CMakeLists.txt" (
        pushd "%ROOT%\.."
        set "ROOT=!CD!"
        popd
    )
)

set "LOG=%ROOT%\EDGE-installer-log.txt"
set "BUILD=%ROOT%\build-win"
set "JUCEDIR=%USERPROFILE%\JUCE"
set "ISS=%ROOT%\packaging\EDGE.iss"

echo EDGE installer-build log > "%LOG%"
echo Started: %DATE% %TIME% >> "%LOG%"
echo Folder: %ROOT% >> "%LOG%"

cls
echo.
echo   ============================================================
echo     EDGE  -  make a Windows installer
echo     by Gussa Naaman
echo   ============================================================
echo.
echo   This builds EDGE and packs it into a single .exe installer
echo   that anyone can run without any developer tools.
echo.

if not exist "%ROOT%\CMakeLists.txt" (
    echo   [X] EDGE's source is not next to this file.
    echo.
    echo       You are probably running this straight out of the ZIP.
    echo       EXTRACT THE WHOLE ZIP to a real folder first.
    echo NO SOURCE >> "%LOG%"
    goto :fail
)

if not exist "%ISS%" (
    echo   [X] packaging\EDGE.iss is missing - the bundle is incomplete.
    echo NO ISS >> "%LOG%"
    goto :fail
)

REM --- 1. the two tools --------------------------------------------------------
echo   [1/4] Checking what is installed...

set "MISSING="

where cmake >nul 2>&1
if errorlevel 1 (
    REM CMake ships inside Visual Studio; look there before giving up.
    for %%P in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    ) do (
        if exist "%%~P\cmake.exe" set "PATH=%%~P;!PATH!"
    )
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo         [X] CMake not found - install Visual Studio 2022 with
    echo             "Desktop development with C++" and run this again.
    set "MISSING=1"
) else (
    echo         [OK] CMake found
)

REM  Inno Setup 6 is the packer. It is free, about 5 MB, and the only thing on
REM  this machine that can produce a real Windows installer.
set "ISCC="
where iscc >nul 2>&1
if not errorlevel 1 for /f "usebackq tokens=*" %%I in (`where iscc`) do set "ISCC=%%I"

if not defined ISCC (
    for %%P in (
        "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
        "%ProgramFiles%\Inno Setup 6\ISCC.exe"
        "%ProgramFiles(x86)%\Inno Setup 5\ISCC.exe"
        "%ProgramFiles%\Inno Setup 5\ISCC.exe"
    ) do (
        if not defined ISCC if exist "%%~P" set "ISCC=%%~P"
    )
)

if defined ISCC (
    echo         [OK] Inno Setup found
    echo ISCC: !ISCC! >> "%LOG%"
) else (
    echo         [X] Inno Setup 6 not found.
    set "MISSING=1"
)

if exist "%JUCEDIR%\CMakeLists.txt" (
    echo         [OK] JUCE found at %JUCEDIR%
) else (
    echo         [X] JUCE is not at %JUCEDIR%.
    echo             Run INSTALL-EDGE.bat once first - it fetches JUCE.
    set "MISSING=1"
)

if defined MISSING (
    echo.
    echo   ============================================================
    echo     Something is missing. Nothing has been produced.
    echo   ============================================================
    echo.
    echo     Inno Setup 6 - free, no account, about 5 MB:
    echo       https://jrsoftware.org/isdl.php
    echo       Install it with its default options, then run this again.
    echo.
    echo     Visual Studio 2022 - free Community edition:
    echo       https://visualstudio.microsoft.com/downloads/
    echo       Tick "Desktop development with C++".
    echo.
    echo MISSING PREREQUISITES >> "%LOG%"
    goto :fail
)

REM --- 2. the version, from the one place it is defined -------------------------
REM  Reading it rather than repeating it: an installer named 0.17 that contains
REM  0.18 is a support call nobody can diagnose from the outside.
set "VER="
for /f "usebackq tokens=3" %%V in (`findstr /b /c:"project(Edge VERSION" "%ROOT%\CMakeLists.txt"`) do set "VER=%%V"
if not defined VER (
    echo   [X] Could not read the version out of CMakeLists.txt.
    echo NO VERSION >> "%LOG%"
    goto :fail
)
echo         [OK] Version %VER%

REM --- 3. build ------------------------------------------------------------------
echo.
echo   [2/4] Building EDGE. The first time takes a few minutes.
echo         Nothing is wrong if it looks stuck; it is compiling.

cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DEDGE_COPY_AFTER_BUILD=OFF >> "%LOG%" 2>&1
if errorlevel 1 (
    echo         [X] Preparation failed. The reason is at the end of:
    echo             %LOG%
    echo CONFIGURE FAILED >> "%LOG%"
    goto :fail
)

cmake --build "%BUILD%" --config Release --target Edge_VST3 Edge_Standalone --parallel >> "%LOG%" 2>&1
if errorlevel 1 (
    echo         [X] The build failed. The compiler's message is at the end of:
    echo             %LOG%
    echo BUILD FAILED >> "%LOG%"
    goto :fail
)

REM  Built is not the same as present. Check the payload inside the bundle, not
REM  just the folder: a half-built VST3 is a folder with nothing in it, and it
REM  installs perfectly and then fails to load.
set "PAYLOAD=%BUILD%\Edge_artefacts\Release\VST3\EDGE.vst3\Contents\x86_64-win\EDGE.vst3"
if not exist "%PAYLOAD%" (
    echo         [X] The build reported success but the plug-in is not there:
    echo             %PAYLOAD%
    dir /s /b "%BUILD%\Edge_artefacts" >> "%LOG%" 2>&1
    echo NO PAYLOAD >> "%LOG%"
    goto :fail
)
if not exist "%BUILD%\Edge_artefacts\Release\Standalone\EDGE.exe" (
    echo         [X] The standalone was not produced.
    echo NO STANDALONE >> "%LOG%"
    goto :fail
)
echo         [OK] Built.

REM --- 4. pack --------------------------------------------------------------------
echo.
echo   [3/4] Packing the installer...

if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"

REM  The version is passed IN so the .iss never has to be edited by hand, and
REM  the icon only if the build actually produced one - Inno refuses to compile
REM  against a SetupIconFile that is not there.
REM  Two calls rather than one with a maybe-empty variable: an empty argument
REM  still arrives as "" and ISCC rejects it as an invalid parameter, which
REM  would fail only on machines where the icon happened to be missing.
set "ICON=%BUILD%\Edge_artefacts\JuceLibraryCode\icon.ico"
if exist "!ICON!" (
    "%ISCC%" /Q "/DAppVersion=%VER%" "/DSrcRoot=%BUILD%\Edge_artefacts\Release" "/DEdgeIcon=!ICON!" "%ISS%" >> "%LOG%" 2>&1
) else (
    "%ISCC%" /Q "/DAppVersion=%VER%" "/DSrcRoot=%BUILD%\Edge_artefacts\Release" "%ISS%" >> "%LOG%" 2>&1
)
if errorlevel 1 (
    echo         [X] Inno Setup refused. Its own message is at the end of:
    echo             %LOG%
    echo ISCC FAILED >> "%LOG%"
    goto :fail
)

REM --- 5. verify ------------------------------------------------------------------
echo.
echo   [4/4] Checking what came out...

set "OUT=%ROOT%\dist\EDGE-%VER%-windows.exe"
if not exist "%OUT%" (
    echo         [X] Inno Setup reported success but there is no installer at:
    echo             %OUT%
    echo NO OUTPUT >> "%LOG%"
    goto :fail
)

REM  An installer that packed nothing is still a valid .exe, roughly 1 MB of
REM  wizard and no payload. EDGE alone is several megabytes, so anything under
REM  two is a package that would install nothing and say it worked.
for %%F in ("%OUT%") do set "BYTES=%%~zF"
if !BYTES! LSS 2000000 (
    echo         [X] The installer is only !BYTES! bytes - it is empty.
    echo             Something was packed without its payload. See the log.
    echo OUTPUT TOO SMALL !BYTES! >> "%LOG%"
    goto :fail
)

set /a MB=!BYTES! / 1048576
echo         [OK] !MB! MB, payload present.

echo.
echo   ============================================================
echo     DONE
echo   ============================================================
echo.
echo     %OUT%
echo.
echo     That single file is the whole product. Send it to anyone.
echo     They double-click it, choose VST3 and/or the standalone,
echo     and EDGE is installed - no tools, no source, no build.
echo.
echo     It installs to:
echo       C:\Program Files\Common Files\VST3\EDGE.vst3
echo       C:\Program Files\Naaman\EDGE\EDGE.exe
echo.
echo     and it appears in Windows' own Apps list, so it uninstalls
echo     the way every other program does.
echo.
echo DONE %OUT% !BYTES! bytes >> "%LOG%"
explorer /select,"%OUT%"
echo   Press any key to close.
pause >nul
exit /b 0

:fail
echo.
echo   Nothing was produced. The full log is:
echo   %LOG%
echo.
echo   Press any key to close.
pause >nul
exit /b 1
