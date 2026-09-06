@echo off
REM ===========================================================================
REM  RunTests.bat - build and run the standalone deterministic-core tests.
REM
REM  Compiles UniverseCore + UniverseGeneration against the shim in
REM  Source/UniverseCore/Public/Standalone and runs every shared test body.
REM  Requires only MSVC (Visual Studio 2022 Build Tools); no Unreal Engine.
REM
REM  Usage:  RunTests.bat [--verbose]
REM ===========================================================================
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..
set BUILD_DIR=%SCRIPT_DIR%Build

REM --- Locate MSVC ----------------------------------------------------------
REM  Done outside any parenthesised block: %ProgramFiles(x86)% contains a ")"
REM  which cmd's block parser treats as the end of the block, producing
REM  confusing errors that do not stop the script.
where cl.exe >nul 2>&1
if not errorlevel 1 goto :have_compiler

set "VSWHERE_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" set "VSWHERE_DIR=%ProgramFiles%\Microsoft Visual Studio\Installer"
if not exist "%VSWHERE_DIR%\vswhere.exe" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 Build Tools
    echo        with the "Desktop development with C++" workload.
    exit /b 1
)

REM  Captured via a temp file rather than a for /f backquote: invoking a
REM  quoted absolute path inside backquotes makes cmd re-parse and mangle the
REM  command line, which fails in a way that does not stop the script.
set "VSPATH="
set "VSPATH_TMP=%TEMP%\universe_vspath.txt"
"%VSWHERE_DIR%\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSPATH_TMP%" 2>nul
if exist "%VSPATH_TMP%" set /p VSPATH=<"%VSPATH_TMP%"
del "%VSPATH_TMP%" >nul 2>&1
if not defined VSPATH (
    echo ERROR: No MSVC x64 C++ toolset found.
    exit /b 1
)

REM  stderr is discarded as well as stdout: vcvars64.bat probes for a VS
REM  instance in a way that prints a harmless "'vswhere.exe' is not recognized"
REM  on some installs while still succeeding. Leaving it visible makes every
REM  build look broken. The errorlevel below is the real success check.
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to initialise the MSVC environment.
    exit /b 1
)

:have_compiler

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM --- Compile --------------------------------------------------------------
REM  /fp:strict     - no reassociation or FMA contraction. The exactness
REM                   arguments in UniverseCoordinates.cpp assume IEEE-754
REM                   semantics exactly as written; /fp:fast would let the
REM                   compiler rewrite them and quietly break determinism.
REM  /W4 /WX        - warnings are errors; this is core numeric code.
REM  /std:c++20     - matches Unreal Engine 5.8's C++ standard.
echo Building standalone core tests...
cl.exe /nologo /std:c++20 /EHsc /O2 /fp:strict /W4 /WX /MT ^
    /DUNIVERSE_STANDALONE=1 ^
    /D_CRT_SECURE_NO_WARNINGS ^
    /I"%SCRIPT_DIR%Shim" ^
    /I"%REPO_ROOT%\Source\UniverseCore\Public" ^
    /I"%REPO_ROOT%\Source\UniverseGeneration\Public" ^
    /I"%REPO_ROOT%\Source\UniversePlanet\Public" ^
    /Fo"%BUILD_DIR%\\" ^
    /Fe"%BUILD_DIR%\UniverseCoreTests.exe" ^
    "%REPO_ROOT%\Source\UniverseCore\Private\UniverseCoordinates.cpp" ^
    "%REPO_ROOT%\Source\UniverseCore\Private\UniverseHash.cpp" ^
    "%REPO_ROOT%\Source\UniverseCore\Private\UniverseSeed.cpp" ^
    "%REPO_ROOT%\Source\UniverseCore\Private\Tests\UniverseCoordinateTests.cpp" ^
    "%REPO_ROOT%\Source\UniverseCore\Private\Tests\UniverseSeedTests.cpp" ^
    "%REPO_ROOT%\Source\UniverseGeneration\Private\StarSystemDescriptor.cpp" ^
    "%REPO_ROOT%\Source\UniverseGeneration\Private\StarSystemGenerator.cpp" ^
    "%REPO_ROOT%\Source\UniverseGeneration\Private\Tests\StarSystemGeneratorTests.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\CubeSphere.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetPatchId.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetSurface.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetTerrain.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetPatchMesh.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetQuadtree.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetGravity.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetSurfaceQuery.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetTrajectory.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\SimulationFrame.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetEnvironment.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetClimate.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetBiome.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetVegetation.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetWeather.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\PlanetEnvironmentQuery.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\Tests\CubeSphereTests.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\Tests\PlanetQuadtreeTests.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\Tests\PlanetTerrainTests.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\Tests\PlanetPatchIdTests.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\Tests\PlanetTraversalTests.cpp" ^
    "%REPO_ROOT%\Source\UniversePlanet\Private\Tests\PlanetEnvironmentTests.cpp" ^
    "%SCRIPT_DIR%StandaloneTestMain.cpp" ^
    /link /SUBSYSTEM:CONSOLE

if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

REM --- Run ------------------------------------------------------------------
echo.
"%BUILD_DIR%\UniverseCoreTests.exe" %*
set TEST_EXIT=%errorlevel%

exit /b %TEST_EXIT%
