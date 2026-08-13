@echo off
setlocal enabledelayedexpansion
title EDGE - Build and Install

REM ---------------------------------------------------------------------------
REM  ONE file. Right-click it, "Run as administrator", and it does everything:
REM  checks what is installed, builds EDGE, installs it, verifies it.
REM
REM  Modelled on FOUR COLOR's installer, which is the one that actually works
REM  on this user's machine. The four things EDGE's old script got wrong and
REM  this one does not:
REM    1. it finds CMake INSIDE Visual Studio instead of demanding it on PATH;
REM    2. it names the generator explicitly instead of letting CMake guess;
REM    3. it uses the SHARED JUCE at %USERPROFILE%\JUCE - downloaded once for
REM       every plug-in, not re-cloned per project;
REM    4. it checks for administrator rights BEFORE a fifteen-minute build.
REM
REM  No text echoed by this script may contain the characters greater-than,
REM  less-than, ampersand, pipe or caret. In an earlier installer a greater-than
REM  inside echoed text became a redirection operator, mangled the output and
REM  broke the if-blocks badly enough to print DONE after NOT INSTALLED.
REM ---------------------------------------------------------------------------

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM  This file ships at the bundle root AND in packaging\. Walk up if the
REM  source is the parent's, so the build always lands in the same place.
if not exist "%ROOT%\CMakeLists.txt" (
    if exist "%ROOT%\..\CMakeLists.txt" (
        pushd "%ROOT%\.."
        set "ROOT=!CD!"
        popd
    )
)

set "LOG=%ROOT%\EDGE-install-log.txt"
set "DEST=C:\Program Files\Common Files\VST3"
set "BUILD=%ROOT%\build-win"
set "JUCEDIR=%USERPROFILE%\JUCE"
set "JUCE_COMMIT=857aab9c4eb3084af639a380a693dcec7d728b73"

echo EDGE build and install log > "%LOG%"
echo Started: %DATE% %TIME% >> "%LOG%"
echo Folder: %ROOT% >> "%LOG%"

cls
echo.
echo   ============================================================
echo     EDGE  -  a two-sided musical filter
echo     by Gussa Naaman
echo   ============================================================
echo.
echo   This builds EDGE from source and installs it.
echo   The first run takes a few minutes. After that it is quick.
echo.

REM --- is the source actually here? --------------------------------------------
if not exist "%ROOT%\CMakeLists.txt" (
    echo   [X] EDGE's source is not next to this file.
    echo.
    echo       You are probably running this straight out of the ZIP.
    echo       Windows copies only the script out that way.
    echo       EXTRACT THE WHOLE ZIP to a real folder first.
    echo NO SOURCE >> "%LOG%"
    goto :fail
)

REM --- 0. administrator ---------------------------------------------------------
net session >nul 2>&1
if errorlevel 1 (
    echo   [X] This needs administrator rights to write into Program Files.
    echo.
    echo       Close this window, RIGHT-CLICK this file, and choose
    echo       "Run as administrator".
    echo NOT ADMIN >> "%LOG%"
    goto :fail
)
echo   [OK] Running as administrator.

REM --- 1. prerequisites ----------------------------------------------------------
echo.
echo   [1/6] Checking what is installed...

set "MISSING="

where cmake >nul 2>&1
if errorlevel 1 (
    REM CMake ships inside Visual Studio; try there before giving up.
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
    echo         [X] CMake not found.
    set "MISSING=1"
) else (
    echo         [OK] CMake found
    cmake --version >> "%LOG%" 2>&1
)

REM  Visual Studio detection is ADVISORY. vswhere is not always present and its
REM  component ids differ between installer versions, so a negative here proves
REM  nothing - CMake is the authority, and it says so plainly a few steps down.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if not exist "%VSWHERE%" goto :vsdone
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSPATH=%%I"
:vsdone

if defined VSPATH (
    echo         [OK] Visual Studio found
    echo VS: !VSPATH! >> "%LOG%"
) else (
    echo         [?] Could not confirm Visual Studio. Carrying on anyway -
    echo             the next step will say plainly if the compiler is missing.
    echo VS NOT DETECTED >> "%LOG%"
)

REM  JUCE 9, SHARED between every plug-in in this workspace. If FOUR COLOR or
REM  any other one was built on this machine, it is already here and nothing
REM  is downloaded.
if exist "%JUCEDIR%\CMakeLists.txt" (
    echo         [OK] JUCE found at %JUCEDIR%
) else (
    echo         [!] JUCE not found at %JUCEDIR%
    where git >nul 2>&1
    if errorlevel 1 (
        echo         [X] ...and git is not installed, so I cannot fetch it.
        set "MISSING=1"
    ) else (
        echo.
        echo         I can download JUCE 9 for you now. It is about 500 MB
        echo         and goes to %JUCEDIR%. This happens once, for every
        echo         plug-in you ever build.
        echo.
        choice /c YN /m "        Download JUCE now"
        if errorlevel 2 (
            echo         Skipped. Nothing was installed.
            echo JUCE DECLINED >> "%LOG%"
            goto :fail
        )
        echo.
        echo         Downloading JUCE, please wait...
        git clone --quiet https://github.com/juce-framework/JUCE.git "%JUCEDIR%" >> "%LOG%" 2>&1
        if errorlevel 1 (
            echo         [X] The download failed. Details are in the log.
            set "MISSING=1"
        ) else (
            pushd "%JUCEDIR%"
            git checkout --quiet %JUCE_COMMIT% >> "%LOG%" 2>&1
            popd
            echo         [OK] JUCE downloaded and set to the pinned version.
        )
    )
)

if defined MISSING (
    echo.
    echo   ============================================================
    echo     Something is missing. Nothing has been changed.
    echo   ============================================================
    echo.
    echo     Visual Studio 2022 - free Community edition:
    echo       https://visualstudio.microsoft.com/downloads/
    echo       In the installer tick "Desktop development with C++".
    echo.
    echo     That one download also provides CMake, so install it first
    echo     and run this file again.
    echo.
    echo MISSING PREREQUISITES >> "%LOG%"
    goto :fail
)

REM --- 2. configure ---------------------------------------------------------------
echo.
echo   [2/6] Preparing the build...
cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DEDGE_COPY_AFTER_BUILD=OFF >> "%LOG%" 2>&1
if errorlevel 1 (
    echo         [X] Preparation failed.
    echo.
    echo             The usual cause is Visual Studio 2022 without the
    echo             "Desktop development with C++" workload. Install it from
    echo             https://visualstudio.microsoft.com/downloads/
    echo             and run this file again.
    echo.
    echo             The exact reason is at the end of:
    echo             %LOG%
    echo CONFIGURE FAILED >> "%LOG%"
    goto :fail
)
echo         [OK] Ready.

REM --- 3. build ---------------------------------------------------------------------
echo.
echo   [3/6] Building. This is the slow part - a few minutes.
echo         Nothing is wrong if it looks stuck; it is compiling.
cmake --build "%BUILD%" --config Release --target Edge_VST3 Edge_Standalone --parallel >> "%LOG%" 2>&1
if errorlevel 1 (
    echo.
    echo         [X] The build failed.
    echo             The compiler's own message is at the end of:
    echo             %LOG%
    echo             Send me the last 40 lines of that file.
    echo BUILD FAILED >> "%LOG%"
    goto :fail
)
echo         [OK] Built.

REM  Where the artefact lands depends on the generator: Visual Studio is
REM  multi-config and inserts a Release\ level, single-config generators do
REM  not. Look for both rather than assuming - and if neither is there, SEARCH
REM  the build tree before giving up, so the log can say what was really made.
set "SRC="
if exist "%BUILD%\Edge_artefacts\Release\VST3\EDGE.vst3\Contents\x86_64-win\EDGE.vst3" (
    set "SRC=%BUILD%\Edge_artefacts\Release\VST3\EDGE.vst3"
)
if not defined SRC if exist "%BUILD%\Edge_artefacts\VST3\EDGE.vst3\Contents\x86_64-win\EDGE.vst3" (
    set "SRC=%BUILD%\Edge_artefacts\VST3\EDGE.vst3"
)
if not defined SRC (
    for /f "delims=" %%F in ('dir /s /b /ad "%BUILD%\EDGE.vst3" 2^>nul') do (
        if not defined SRC if exist "%%F\Contents\x86_64-win\EDGE.vst3" set "SRC=%%F"
    )
)

if not defined SRC (
    echo         [X] The build reported success but no EDGE.vst3 with a
    echo             Windows binary inside it exists under:
    echo             %BUILD%
    echo MISSING ARTEFACT - what was actually built: >> "%LOG%"
    dir /s /b "%BUILD%\Edge_artefacts" >> "%LOG%" 2>&1
    goto :fail
)
echo         found: !SRC!
echo ARTEFACT: !SRC! >> "%LOG%"

REM --- 4. a running DAW will block the copy -----------------------------------------
echo.
echo   [4/6] Checking for a running DAW...
set "DAW="
for %%P in ("Cubase.exe" "Cubase14.exe" "Cubase15.exe" "Nuendo.exe" "Ableton Live.exe" "FL64.exe" "reaper.exe" "Studio One.exe" "Bitwig Studio.exe") do (
    tasklist /fi "imagename eq %%~P" 2>nul | find /i "%%~P" >nul && set "DAW=%%~P"
)
if defined DAW (
    echo.
    echo         [!] !DAW! is running.
    echo             Windows will not replace a plug-in a host has loaded, and
    echo             this is the usual reason an update seems to do nothing.
    echo.
    echo             Close it now, then press any key.
    echo DAW RUNNING: !DAW! >> "%LOG%"
    pause >nul
) else (
    echo         [OK] Nothing in the way.
)

REM --- 5. install ---------------------------------------------------------------------
echo.
echo   [5/6] Installing...
if not exist "%DEST%\" mkdir "%DEST%" 2>nul

REM  An old install can be a folder bundle OR a stray single file. rmdir cannot
REM  delete a file and xcopy cannot create a folder whose name a file has taken,
REM  and both fail quietly. Handle both, then verify the removal.
if exist "%DEST%\EDGE.vst3\" (
    rmdir /s /q "%DEST%\EDGE.vst3"
) else (
    if exist "%DEST%\EDGE.vst3" del /f /q "%DEST%\EDGE.vst3"
)

if exist "%DEST%\EDGE.vst3" (
    echo         [X] The old version could not be removed. Something still has
    echo             it open - a DAW, or the plug-in scanner that keeps running
    echo             after one closes. Close every audio application and run
    echo             this again.
    echo REMOVE FAILED >> "%LOG%"
    goto :fail
)

xcopy /e /i /y "!SRC!" "%DEST%\EDGE.vst3\" >> "%LOG%" 2>&1
if errorlevel 1 (
    echo         [X] The copy failed. xcopy's own message is in the log.
    echo COPY FAILED >> "%LOG%"
    goto :fail
)
echo         [OK] Plug-in installed.

set "EXE=%BUILD%\Edge_artefacts\Release\Standalone\EDGE.exe"
if not exist "!EXE!" set "EXE=%BUILD%\Edge_artefacts\Standalone\EDGE.exe"
if exist "!EXE!" (
    if not exist "C:\Program Files\Naaman\EDGE\" mkdir "C:\Program Files\Naaman\EDGE" 2>nul
    copy /y "!EXE!" "C:\Program Files\Naaman\EDGE\EDGE.exe" >nul 2>&1
    echo         [OK] Standalone app installed.
)

REM --- 6. verify -------------------------------------------------------------------------
echo.
echo   [6/6] Verifying...
if not exist "%DEST%\EDGE.vst3\Contents\x86_64-win\EDGE.vst3" (
    echo         [X] The binary is not where it should be after copying.
    echo             Wanted:
    echo             %DEST%\EDGE.vst3\Contents\x86_64-win\EDGE.vst3
    echo VERIFY FAILED - what is actually there: >> "%LOG%"
    dir /s /b "%DEST%\EDGE.vst3" >> "%LOG%" 2>&1
    goto :fail
)
echo         [OK] Verified.

echo INSTALL OK >> "%LOG%"
echo.
echo   ============================================================
echo     DONE. EDGE is installed.
echo.
echo     %DEST%\EDGE.vst3
echo.
echo     1. Start Cubase
echo     2. Studio menu, then VST Plug-in Manager, then Update
echo     3. EDGE is filed under Filter. If you do not see it, type
echo        "EDGE" in the plug-in search box.
echo.
echo     To check this build measures correctly on your machine,
echo     run RUN-EDGE-TESTS.bat - it is optional and takes a minute.
echo   ============================================================
echo.
pause
exit /b 0

:fail
echo.
echo   ------------------------------------------------
echo    NOT INSTALLED.
echo    Send me this file:
echo    %LOG%
echo   ------------------------------------------------
echo.
pause
exit /b 1
