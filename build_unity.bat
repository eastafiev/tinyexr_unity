@echo off
setlocal

REM ============================================
REM Build Unity TinyEXR plugin (UnityPlugin CMake)
REM Stages package to repo-root dist\ and opens it.
REM ============================================

echo.
echo ============================================
echo Building Unity TinyEXR Plugin (Standalone)
echo ============================================
echo.

REM Paths relative to this script (repo root)
set "SOURCE_DIR=%~dp0"
set "SOURCE_DIR=%SOURCE_DIR:~0,-1%"
set "BUILD_DIR=%SOURCE_DIR%\build_unity"
set "DIST_DIR=%SOURCE_DIR%\dist"
set "UNITY_PROJECT=%~1"

REM Clean build directory
if exist "%BUILD_DIR%" (
    echo Cleaning build directory...
    rmdir /s /q "%BUILD_DIR%"
)

REM Create build directory
mkdir "%BUILD_DIR%"

REM Configure - use UnityPlugin/CMakeLists.txt directly
echo Configuring CMake...
cd /d "%BUILD_DIR%"

if "%UNITY_PROJECT%"=="" (
    cmake -G "Visual Studio 17 2022" -A x64 "%SOURCE_DIR%\UnityPlugin"
) else (
    cmake -G "Visual Studio 17 2022" -A x64 -DUNITY_PROJECT_PATH="%UNITY_PROJECT%" "%SOURCE_DIR%\UnityPlugin"
)

if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

REM Build
echo.
echo Building...
cmake --build . --config Release

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo ============================================
echo BUILD SUCCESSFUL!
echo ============================================
echo Package: %DIST_DIR%\
if not "%UNITY_PROJECT%"=="" (
    echo Deployed to: %UNITY_PROJECT%\Assets\
)
echo ============================================
echo.

REM Open staged package folder in Explorer
if exist "%DIST_DIR\" (
    explorer "%DIST_DIR%"
)

exit /b 0
