@echo off
setlocal enabledelayedexpansion
title EDGE - Windows build

rem ---------------------------------------------------------------------------
rem  Builds EDGE's VST3 from source. Clones JUCE itself at a pinned commit, so
rem  there is nothing to configure.
rem
rem  Every step is checked. This script must never print "done" for something it
rem  did not do - a silent installer failure costs more than a loud one.
rem ---------------------------------------------------------------------------

set "JUCE_COMMIT=857aab9c4eb3084af639a380a693dcec7d728b73"
set "ROOT=%~dp0"

rem  This script ships BOTH at the bundle root and in packaging\. Run from
rem  packaging\ it would build into packaging\build, which is not where
rem  EDGE.iss looks. Walk up one level when the source is the parent's.
if not exist "%ROOT%CMakeLists.txt" (
  if exist "%ROOT%..\CMakeLists.txt" (
    pushd "%ROOT%.."
    set "ROOT=!CD!\"
    popd
  )
)

cd /d "%ROOT%"

echo.
echo   EDGE - building the VST3
echo   ------------------------
echo   Folder: %ROOT%
echo.

rem --- is the source actually here? -------------------------------------------
rem  Double-clicking this .bat from inside Explorer's ZIP viewer extracts the
rem  script ALONE to a temp folder. Everything below would then fail with a
rem  confusing CMake error instead of the real reason.
if not exist "%ROOT%CMakeLists.txt" (
  echo   ERROR: EDGE's source is not next to this script.
  echo.
  echo          You are probably running build-windows.bat straight out of
  echo          the ZIP. Windows extracts only the script that way.
  echo.
  echo          EXTRACT THE WHOLE ZIP to a real folder first, then run
  echo          build-windows.bat from inside that folder.
  goto :fail
)

rem --- the two tools we cannot do without ------------------------------------
where cmake >nul 2>nul
if errorlevel 1 (
  echo   ERROR: cmake is not on your PATH.
  echo          Install CMake and tick "Add CMake to the system PATH",
  echo          then open a NEW terminal and run this again.
  goto :fail
)

where git >nul 2>nul
if errorlevel 1 (
  echo   ERROR: git is not on your PATH.
  echo          Install Git for Windows, then open a NEW terminal.
  goto :fail
)

rem --- JUCE, at the exact commit this was tested against ----------------------
if exist "JUCE\CMakeLists.txt" (
  echo   JUCE is already here - leaving it alone.
) else (
  echo   Cloning JUCE ... this is the slow part, once.
  git clone --quiet https://github.com/juce-framework/JUCE.git JUCE
  if errorlevel 1 (
    echo   ERROR: could not clone JUCE. Check your internet connection.
    goto :fail
  )

  pushd JUCE
  git checkout --quiet %JUCE_COMMIT%
  if errorlevel 1 (
    popd
    echo   ERROR: could not check out the pinned JUCE commit %JUCE_COMMIT%.
    goto :fail
  )
  popd
)

if not exist "JUCE\CMakeLists.txt" (
  echo   ERROR: JUCE\CMakeLists.txt is missing after the clone.
  goto :fail
)

rem --- configure --------------------------------------------------------------
echo.
echo   Configuring ...
cmake -B build -DCMAKE_BUILD_TYPE=Release -DEDGE_COPY_AFTER_BUILD=OFF
if errorlevel 1 (
  echo.
  echo   ERROR: cmake configure failed - see the message above.
  echo          "No CMAKE_CXX_COMPILER" means Visual Studio is missing the
  echo          "Desktop development with C++" workload.
  goto :fail
)

rem --- build ------------------------------------------------------------------
echo.
echo   Building ... 5-15 minutes the first time.
cmake --build build --config Release --target Edge_VST3 Edge_Standalone EdgeTests EdgeHostTests
if errorlevel 1 (
  echo.
  echo   ERROR: the build failed - see the message above.
  goto :fail
)

set "VST3=%ROOT%build\Edge_artefacts\Release\VST3\EDGE.vst3"
if not exist "%VST3%" (
  echo.
  echo   ERROR: the build reported success but %VST3% does not exist.
  goto :fail
)

echo.
echo   BUILT:  %VST3%
echo.

rem --- measure ----------------------------------------------------------------
rem  The DSP was measured on macOS. Nothing about that transfers to a different
rem  compiler and a different floating-point back end, so the same suite runs
rem  here and prints the same numbers - or this script stops.
set "TESTS=%ROOT%build\EdgeTests_artefacts\Release\EdgeTests.exe"
if not exist "%TESTS%" (
  echo   ERROR: the test suite was not built: %TESTS% is missing.
  goto :fail
)

echo   Running the measurement suite ...
echo   ------------------------------------------------------------------
"%TESTS%"
if errorlevel 1 (
  echo   ------------------------------------------------------------------
  echo.
  echo   ERROR: the measurement suite FAILED on this machine.
  echo          Every check prints the value it measured - the failing lines
  echo          above say which number is wrong. Do not install this build.
  goto :fail
)
echo   ------------------------------------------------------------------
echo.

set "HOSTTESTS=%ROOT%build\EdgeHostTests_artefacts\Release\EdgeHostTests.exe"
if not exist "%HOSTTESTS%" (
  echo   ERROR: the host-contract suite was not built: %HOSTTESTS% is missing.
  goto :fail
)

echo   Running the host-contract suite ...
echo   ------------------------------------------------------------------
"%HOSTTESTS%"
if errorlevel 1 (
  echo   ------------------------------------------------------------------
  echo.
  echo   ERROR: the host-contract suite FAILED on this machine.
  echo          Do not install this build.
  goto :fail
)
echo   ------------------------------------------------------------------
echo   Every check passed on this machine.
echo.

rem --- optional install -------------------------------------------------------
set "TARGET=%CommonProgramFiles%\VST3"
set "DEST=%TARGET%\EDGE.vst3"
echo   Copy it to %TARGET% ? That is where Cubase looks.
set /p ANSWER=  [y/N] 

if /i not "%ANSWER%"=="y" (
  echo.
  echo   Left in the build folder. Point Cubase at it, or copy it yourself.
  goto :done
)

rem  A running DAW holds the plug-in open and Windows refuses to replace it -
rem  silently, from this script's point of view. Check first and say so.
set "DAWRUNNING="
rem  Quoted, and dereferenced with %%~P: a bare `for` list splits on spaces,
rem  so "Studio One.exe" would have become two bogus process names.
for %%P in ("Cubase.exe" "Nuendo.exe" "Ableton Live.exe" "reaper.exe" "FL64.exe" "Studio One.exe" "Bitwig Studio.exe") do (
  tasklist /FI "IMAGENAME eq %%~P" 2>nul | find /I "%%~P" >nul && set "DAWRUNNING=%%~P"
)
if defined DAWRUNNING (
  echo.
  echo   ERROR: %DAWRUNNING% is running. Windows will not let this script
  echo          replace a plug-in a DAW has open, and the failure is silent.
  echo          Close it and run this again.
  goto :fail
)

rem  An old install can be a single FILE rather than a folder bundle. rmdir
rem  cannot delete a file, xcopy cannot create a folder whose name a file has
rem  taken, and BOTH fail quietly. Handle both shapes, then VERIFY.
if exist "%DEST%\" (
  echo   Removing the previous folder install ...
  rmdir /S /Q "%DEST%"
) else (
  if exist "%DEST%" (
    echo   Removing the previous FILE install ...
    del /F /Q "%DEST%"
  )
)

if exist "%DEST%" (
  echo.
  echo   ERROR: the previous install at
  echo       %DEST%
  echo   could not be removed. This is almost always administrator rights or
  echo   a DAW still holding it. Nothing has been changed.
  goto :fail
)

echo   Copying ...
rem  No >nul here: if xcopy has something to say, the user needs to read it.
xcopy /E /I /Y "%VST3%" "%DEST%"
if errorlevel 1 (
  echo.
  echo   COPY FAILED - almost always because this needs administrator rights.
  echo   Right-click build-windows.bat and choose "Run as administrator",
  echo   or copy this folder by hand:
  echo       from  %VST3%
  echo       to    %DEST%
  goto :fail
)

rem  Verify the PAYLOAD, not just the folder: an empty EDGE.vst3 directory
rem  counts as "exists" and Cubase would scan it and fail.
if not exist "%DEST%\Contents\x86_64-win\EDGE.vst3" (
  echo.
  echo   COPY FAILED: the bundle arrived incomplete.
  echo       expected %DEST%\Contents\x86_64-win\EDGE.vst3
  dir "%DEST%" /S /B
  goto :fail
)

echo   Installed to %DEST%
echo   (verified: Contents\x86_64-win\EDGE.vst3 is on disk)
echo.
echo   In Cubase: Studio ^> VST Plug-in Manager ^> rescan. EDGE is under Filter.

:done
echo.
echo   Finished.
pause
exit /b 0

:fail
echo.
echo   STOPPED. Nothing was installed.
pause
exit /b 1
