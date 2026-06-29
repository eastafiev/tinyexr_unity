@echo off
setlocal

echo.
echo ============================================
echo Building Unity TinyEXR Plugin (Standalone)
echo ============================================
echo.

REM Set paths using C:\D\ format
set "SOURCE_DIR=C:\D\Projects_My\TinyExrUnity"
set "BUILD_DIR=C:\D\Projects_My\TinyExrUnity\build_unity"
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
cd "%BUILD_DIR%"

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
echo Output: %BUILD_DIR%\bin\UnityTinyEXR.dll
if not "%UNITY_PROJECT%"=="" (
    echo Deployed to: %UNITY_PROJECT%\Assets\Plugins\
)
echo ============================================
echo.

exit /b 0
