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
cd /d "%ROOT%"

echo.
echo   EDGE - building the VST3
echo   ------------------------
echo   Folder: %ROOT%
echo.

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
echo   Copy it to %TARGET% ? That is where Cubase looks.
set /p ANSWER=  [y/N] 

if /i not "%ANSWER%"=="y" (
  echo.
  echo   Left in the build folder. Point Cubase at it, or copy it yourself.
  goto :done
)

echo   Copying ...
xcopy /E /I /Y /Q "%VST3%" "%TARGET%\EDGE.vst3" >nul
if errorlevel 1 (
  echo.
  echo   COPY FAILED - almost always because this needs administrator rights.
  echo   Right-click build-windows.bat and choose "Run as administrator",
  echo   or copy this folder by hand:
  echo       from  %VST3%
  echo       to    %TARGET%\EDGE.vst3
  goto :fail
)

if not exist "%TARGET%\EDGE.vst3" (
  echo   COPY FAILED: %TARGET%\EDGE.vst3 does not exist afterwards.
  goto :fail
)

echo   Installed to %TARGET%\EDGE.vst3
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
