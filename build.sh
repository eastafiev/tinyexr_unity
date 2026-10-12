#!/bin/bash

# ============================================
# TinyExrUnity Build Script (Unix)
# ============================================

set -e

BUILD_TYPE="Release"
BUILD_DIR="build"
UNITY_PROJECT=""
CLEAN=0
VERBOSE=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --unity)
            UNITY_PROJECT="$2"
            shift 2
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --verbose)
            VERBOSE="--verbose"
            shift
            ;;
        --help)
            cat << EOF
TinyExrUnity Build Script

Usage: ./build.sh [options]

Options:
  --debug          Build in Debug mode
  --unity PATH     Deploy to Unity project
  --clean          Clean before building
  --verbose        Verbose build output
  --help           Show this help

Examples:
  ./build.sh
  ./build.sh --debug --verbose
  ./build.sh --unity "/path/to/UnityProject"

EOF
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage"
            exit 1
            ;;
    esac
done

echo ""
echo "============================================"
echo "Building TinyExrUnity Plugin"
echo "============================================"
echo "Build Type: $BUILD_TYPE"
echo "Build Dir:  $BUILD_DIR"
[[ -n "$UNITY_PROJECT" ]] && echo "Unity:      $UNITY_PROJECT"
echo "============================================"
echo ""

# Clean
if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure
echo "Configuring CMake..."
CMAKE_ARGS="-G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE"
[[ -n "$UNITY_PROJECT" ]] && CMAKE_ARGS="$CMAKE_ARGS -DUNITY_PROJECT_PATH=$UNITY_PROJECT"

cmake $CMAKE_ARGS ..

# Build
echo ""
echo "Building..."
cmake --build . --config "$BUILD_TYPE" $VERBOSE

cd ..
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$SCRIPT_DIR/dist"

echo ""
echo "============================================"
echo "BUILD SUCCESSFUL"
echo "============================================"
echo "Package: $DIST_DIR/"
[[ -n "$UNITY_PROJECT" ]] && echo "Deployed to: $UNITY_PROJECT/Assets/"
echo "============================================"
echo ""

# Open staged package folder in the OS file manager
if [[ -d "$DIST_DIR" ]]; then
    if [[ "$(uname)" == "Darwin" ]]; then
        open "$DIST_DIR"
    elif command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$DIST_DIR" >/dev/null 2>&1 || true
    fi
fi
