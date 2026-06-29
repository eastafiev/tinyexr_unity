@echo off
setlocal enabledelayedexpansion

REM ============================================
REM TinyExrUnity Build Script (Windows)
REM ============================================

set "BUILD_TYPE=Release"
set "BUILD_DIR=build"
set "UNITY_PROJECT="
set "CLEAN=0"
set "VERBOSE="

REM Parse arguments
:parse
if "%~1"=="" goto build
if /i "%~1"=="--debug" (
    set "BUILD_TYPE=Debug"
    shift
    goto parse
)
if /i "%~1"=="--unity" (
    set "UNITY_PROJECT=%~2"
    shift
    shift
    goto parse
)
if /i "%~1"=="--clean" (
    set "CLEAN=1"
    shift
    goto parse
)
if /i "%~1"=="--verbose" (
    set "VERBOSE=--verbose"
    shift
    goto parse
)
if /i "%~1"=="--help" goto help
shift
goto parse

:build
echo.
echo ============================================
echo Building TinyExrUnity Plugin
echo ============================================
echo Build Type: %BUILD_TYPE%
echo Build Dir:  %BUILD_DIR%
if defined UNITY_PROJECT echo Unity:      %UNITY_PROJECT%
echo ============================================
echo.

REM Clean if requested
if %CLEAN%==1 (
    echo Cleaning %BUILD_DIR%...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

REM Configure
echo Configuring CMake...

REM Set x64 toolchain for MSVC
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul

set CMAKE_ARGS=-G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if defined UNITY_PROJECT set CMAKE_ARGS=%CMAKE_ARGS% -DUNITY_PROJECT_PATH=%UNITY_PROJECT%

cmake %CMAKE_ARGS% ..
if errorlevel 1 goto error

REM Build
echo.
echo Building...
cmake --build . --config %BUILD_TYPE% %VERBOSE%
if errorlevel 1 goto error

cd ..
echo.
echo ============================================
echo BUILD SUCCESSFUL
echo ============================================
echo Output: %BUILD_DIR%\bin\
if defined UNITY_PROJECT (
    echo Deployed to: %UNITY_PROJECT%\Assets\Plugins\
)
echo ============================================
echo.
exit /b 0

:error
cd ..
echo.
echo ============================================
echo BUILD FAILED
echo ============================================
echo.
exit /b 1

:help
echo.
echo TinyExrUnity Build Script
echo.
echo Usage: build.bat [options]
echo.
echo Options:
echo   --debug          Build in Debug mode
echo   --unity PATH     Deploy to Unity project
echo   --clean          Clean before building
echo   --verbose        Verbose build output
echo   --help           Show this help
echo.
echo Examples:
echo   build.bat
echo   build.bat --debug --verbose
echo   build.bat --unity "C:\MyUnityProject"
echo.
exit /b 0
