@echo off
setlocal enabledelayedexpansion

REM ═══════════════════════════════════════════════════════════════════════════
REM  HorizonEngine Build Script (Windows)
REM
REM  Usage:
REM    build.bat                     Build editor (Release)
REM    build.bat release             Build editor (Release)
REM    build.bat debug               Build editor (Debug)
REM    build.bat runtime             Build runtime only (Release)
REM    build.bat runtime debug       Build runtime only (Debug)
REM    build.bat configure           Configure only (no build)
REM    build.bat clean               Remove build directory
REM    build.bat bootstrap           Run bootstrap to install tools
REM
REM  The script automatically detects tools from:
REM    1. Tools/ directory (portable, from bootstrap.ps1)
REM    2. Visual Studio installation
REM    3. System PATH
REM ═══════════════════════════════════════════════════════════════════════════

set "ENGINE_ROOT=%~dp0"
set "BUILD_DIR=%ENGINE_ROOT%build"
set "TOOLS_DIR=%ENGINE_ROOT%Tools"
set "BUILD_CONFIG=Release"
set "BUILD_TARGET=HorizonEngine"
set "CONFIGURE_ONLY=0"
set "CLEAN_BUILD=0"
set "RUN_BOOTSTRAP=0"

REM ── Parse arguments ─────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="release"   ( set "BUILD_CONFIG=Release"        & shift & goto :parse_args )
if /i "%~1"=="debug"     ( set "BUILD_CONFIG=Debug"           & shift & goto :parse_args )
if /i "%~1"=="relwithdebinfo" ( set "BUILD_CONFIG=RelWithDebInfo" & shift & goto :parse_args )
if /i "%~1"=="runtime"   ( set "BUILD_TARGET=HorizonEngineRuntime" & shift & goto :parse_args )
if /i "%~1"=="editor"    ( set "BUILD_TARGET=HorizonEngine"   & shift & goto :parse_args )
if /i "%~1"=="configure" ( set "CONFIGURE_ONLY=1"             & shift & goto :parse_args )
if /i "%~1"=="clean"     ( set "CLEAN_BUILD=1"                & shift & goto :parse_args )
if /i "%~1"=="bootstrap" ( set "RUN_BOOTSTRAP=1"              & shift & goto :parse_args )
echo Unknown argument: %~1
shift
goto :parse_args
:args_done

REM ── Bootstrap ───────────────────────────────────────────────────────────
if "%RUN_BOOTSTRAP%"=="1" (
    echo Running bootstrap...
    powershell -ExecutionPolicy Bypass -File "%ENGINE_ROOT%tools\bootstrap.ps1"
    exit /b %ERRORLEVEL%
)

REM ── Clean ───────────────────────────────────────────────────────────────
if "%CLEAN_BUILD%"=="1" (
    echo Cleaning build directory: %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Done.
    exit /b 0
)

REM ── Load portable tools environment ─────────────────────────────────────
if exist "%TOOLS_DIR%\env.bat" (
    call "%TOOLS_DIR%\env.bat"
)

REM ── Detect CMake ────────────────────────────────────────────────────────
set "CMAKE_EXE="

REM 1. Bundled
if exist "%TOOLS_DIR%\cmake\bin\cmake.exe" (
    set "CMAKE_EXE=%TOOLS_DIR%\cmake\bin\cmake.exe"
    goto :cmake_found
)

REM 2. VS-bundled via vswhere
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath 2^>nul`) do (
        set "VS_PATH=%%i"
    )
    if defined VS_PATH (
        if exist "!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "CMAKE_EXE=!VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            goto :cmake_found
        )
    )
)

REM 3. System PATH
where cmake >nul 2>&1
if %ERRORLEVEL%==0 (
    for /f "delims=" %%i in ('where cmake 2^>nul') do (
        set "CMAKE_EXE=%%i"
        goto :cmake_found
    )
)

echo.
echo [ERROR] CMake not found!
echo   Run:  build.bat bootstrap
echo   Or install CMake from https://cmake.org/download/
exit /b 1

:cmake_found
echo CMake: %CMAKE_EXE%

REM ── Detect Generator and Compiler ───────────────────────────────────────
set "CMAKE_GENERATOR="
set "CMAKE_COMPILER_ARGS="

REM Check for Ninja
set "NINJA_EXE="
if exist "%TOOLS_DIR%\ninja\ninja.exe" set "NINJA_EXE=%TOOLS_DIR%\ninja\ninja.exe"
if not defined NINJA_EXE (
    where ninja >nul 2>&1
    if !ERRORLEVEL!==0 (
        for /f "delims=" %%i in ('where ninja 2^>nul') do set "NINJA_EXE=%%i"
    )
)

REM Check for Clang (bundled)
set "CLANG_EXE="
if exist "%TOOLS_DIR%\llvm\bin\clang++.exe" set "CLANG_EXE=%TOOLS_DIR%\llvm\bin\clang++.exe"
if not defined CLANG_EXE (
    where clang++ >nul 2>&1
    if !ERRORLEVEL!==0 (
        for /f "delims=" %%i in ('where clang++ 2^>nul') do set "CLANG_EXE=%%i"
    )
)

REM Check for MSVC via vswhere
set "MSVC_FOUND=0"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        set "MSVC_FOUND=1"
    )
)

REM Strategy: prefer MSVC (VS generator), then Clang+Ninja, then Ninja alone
if "%MSVC_FOUND%"=="1" (
    REM Use Visual Studio generator (auto-detected version)
    echo Compiler: MSVC (Visual Studio)
    REM Let CMake auto-detect the VS generator
) else if defined CLANG_EXE (
    if defined NINJA_EXE (
        set "CMAKE_GENERATOR=-G Ninja"
        set "CMAKE_COMPILER_ARGS=-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
        echo Compiler: Clang + Ninja
    ) else (
        set "CMAKE_COMPILER_ARGS=-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
        echo Compiler: Clang (default generator)
    )
) else if defined NINJA_EXE (
    set "CMAKE_GENERATOR=-G Ninja"
    echo Build system: Ninja
) else (
    echo [WARNING] No compiler or Ninja found. CMake will use defaults.
)

REM ── Configure ───────────────────────────────────────────────────────────
echo.
echo ── CMake Configure (%BUILD_CONFIG%) ──
echo.

set "CONFIGURE_CMD="%CMAKE_EXE%" -S "%ENGINE_ROOT%." -B "%BUILD_DIR%""
if defined CMAKE_GENERATOR set "CONFIGURE_CMD=%CONFIGURE_CMD% %CMAKE_GENERATOR%"
if defined CMAKE_COMPILER_ARGS set "CONFIGURE_CMD=%CONFIGURE_CMD% %CMAKE_COMPILER_ARGS%"

echo ^> %CONFIGURE_CMD%
%CONFIGURE_CMD%
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] CMake configure failed!
    exit /b %ERRORLEVEL%
)

if "%CONFIGURE_ONLY%"=="1" (
    echo.
    echo Configure complete. Build directory: %BUILD_DIR%
    exit /b 0
)

REM ── Build ───────────────────────────────────────────────────────────────
echo.
echo ── CMake Build (%BUILD_TARGET%, %BUILD_CONFIG%) ──
echo.

set "BUILD_CMD="%CMAKE_EXE%" --build "%BUILD_DIR%" --target %BUILD_TARGET% --config %BUILD_CONFIG%"
echo ^> %BUILD_CMD%
%BUILD_CMD%
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ══════════════════════════════════════════════════
echo   Build successful: %BUILD_TARGET% (%BUILD_CONFIG%)
echo ══════════════════════════════════════════════════
echo.

endlocal
