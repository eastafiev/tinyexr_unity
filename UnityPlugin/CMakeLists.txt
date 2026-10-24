# Standalone UnityPlugin CMake: build UnityTinyEXR and stage repo-root dist/ package.
cmake_minimum_required(VERSION 3.5)
project(UnityTinyEXRPlugin VERSION 1.0.0 LANGUAGES C CXX)

# C++11 standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(USE_MINIZ "Use miniz instead of system zlib" ON)
set(UNITY_PROJECT_PATH "" CACHE PATH "Path to Unity project for deployment")
# Repo-root dist/ (this file lives in UnityPlugin/)
set(DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist" CACHE PATH "Unity Assets-shaped package output directory")

# Source files
set(PLUGIN_SOURCES
    native/unity_tinyexr_plugin.cpp
)

set(PLUGIN_HEADERS
    native/unity_tinyexr_plugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../tinyexr.h
)

# Compression library - look for miniz in parent directory
if(USE_MINIZ)
    set(MINIZ_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../deps/miniz/miniz.c")
    if(EXISTS "${MINIZ_PATH}")
        list(APPEND PLUGIN_SOURCES ${MINIZ_PATH})
        message(STATUS "Unity Plugin: Using miniz from ${MINIZ_PATH}")
    else()
        message(WARNING "miniz.c not found at ${MINIZ_PATH}, trying system zlib")
        set(USE_MINIZ OFF)
    endif()
endif()

if(NOT USE_MINIZ)
    find_package(ZLIB REQUIRED)
    message(STATUS "Unity Plugin: Using system zlib: ${ZLIB_LIBRARIES}")
endif()

# Create shared library (DLL/SO/DYLIB)
add_library(UnityTinyEXR SHARED ${PLUGIN_SOURCES})

# Include directories
target_include_directories(UnityTinyEXR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/native
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/miniz
)

# Link libraries
if(NOT USE_MINIZ)
    target_link_libraries(UnityTinyEXR PRIVATE ZLIB::ZLIB)
endif()

# Compile definitions
target_compile_definitions(UnityTinyEXR PRIVATE
    UNITY_TINYEXR_EXPORTS=1
    TINYEXR_USE_THREAD=1
    $<$<BOOL:${USE_MINIZ}>:TINYEXR_USE_MINIZ=1>
)

if(MSVC)
    target_compile_definitions(UnityTinyEXR PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

if(WIN32)
    target_link_libraries(UnityTinyEXR PRIVATE psapi)
endif()

# Standalone resize benchmark. Compiles the plugin sources so it can call
# LoadEXR directly as the bit-exact reference without a new DLL export.
add_executable(exr_resize_bench
    native/exr_resize_bench.cpp
    ${PLUGIN_SOURCES}
)
target_include_directories(exr_resize_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/native
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../deps/miniz
)
target_compile_definitions(exr_resize_bench PRIVATE
    UNITY_TINYEXR_EXPORTS=1
    TINYEXR_USE_THREAD=1
    $<$<BOOL:${USE_MINIZ}>:TINYEXR_USE_MINIZ=1>
)
if(NOT USE_MINIZ)
    target_link_libraries(exr_resize_bench PRIVATE ZLIB::ZLIB)
endif()
if(WIN32)
    target_link_libraries(exr_resize_bench PRIVATE psapi)
endif()
if(MSVC)
    target_compile_definitions(exr_resize_bench PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()
set_target_properties(exr_resize_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Platform-specific output settings
if(WIN32)
    set_target_properties(UnityTinyEXR PROPERTIES
        OUTPUT_NAME "UnityTinyEXR"
        PREFIX ""
        SUFFIX ".dll"
    )
elseif(APPLE)
    set_target_properties(UnityTinyEXR PROPERTIES
        OUTPUT_NAME "UnityTinyEXR"
        PREFIX "lib"
        SUFFIX ".bundle"
        MACOSX_RPATH ON
        BUILD_WITH_INSTALL_RPATH ON
        INSTALL_NAME_DIR "@rpath"
    )
else()
    set_target_properties(UnityTinyEXR PROPERTIES
        OUTPUT_NAME "UnityTinyEXR"
        PREFIX "lib"
        SUFFIX ".so"
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Output to bin directory
set_target_properties(UnityTinyEXR PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Platform folder under dist/Plugins (matches Unity Assets/Plugins layout)
if(WIN32)
    set(DIST_PLATFORM_DIR "${DIST_DIR}/Plugins/x86_64")
elseif(APPLE)
    set(DIST_PLATFORM_DIR "${DIST_DIR}/Plugins/macOS")
else()
    set(DIST_PLATFORM_DIR "${DIST_DIR}/Plugins/Linux/x86_64")
endif()
set(DIST_SCRIPTS_DIR "${DIST_DIR}/Scripts/TinyEXR")
set(DIST_EXAMPLES_DIR "${DIST_SCRIPTS_DIR}/Examples")
# Real loader sources. TinyEXRLoader.cs is not in the tree; the example calls UTinyEXRLoader.
set(CSHARP_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/csharp")
set(EXAMPLE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/examples/EXRLoaderExample.cs")

# Always stage DLL + C# API + sample script into repo-root dist/ after build.
# Scripts stay source files; they are not compiled into the native plugin.
add_custom_command(TARGET UnityTinyEXR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_PLATFORM_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_SCRIPTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_EXAMPLES_DIR}"
    # Drop the old staged name so it cannot duplicate class UTinyEXRLoader.
    COMMAND ${CMAKE_COMMAND} -E remove -f "${DIST_SCRIPTS_DIR}/TinyEXRLoader.cs"
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:UnityTinyEXR>
        "${DIST_PLATFORM_DIR}/$<TARGET_FILE_NAME:UnityTinyEXR>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CSHARP_SRC_DIR}/UTinyEXRLoader.cs"
        "${DIST_SCRIPTS_DIR}/UTinyEXRLoader.cs"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CSHARP_SRC_DIR}/NativeExrBuffer.cs"
        "${DIST_SCRIPTS_DIR}/NativeExrBuffer.cs"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CSHARP_SRC_DIR}/ExrRawImageData.cs"
        "${DIST_SCRIPTS_DIR}/ExrRawImageData.cs"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${EXAMPLE_SCRIPT}"
        "${DIST_EXAMPLES_DIR}/EXRLoaderExample.cs"
    COMMENT "Staging package to: ${DIST_DIR}"
)

# Optional: also copy staged layout into a live Unity project's Assets/
if(UNITY_PROJECT_PATH AND EXISTS "${UNITY_PROJECT_PATH}")
    if(WIN32)
        set(UNITY_PLATFORM_DIR "${UNITY_PROJECT_PATH}/Assets/Plugins/x86_64")
    elseif(APPLE)
        set(UNITY_PLATFORM_DIR "${UNITY_PROJECT_PATH}/Assets/Plugins/macOS")
    else()
        set(UNITY_PLATFORM_DIR "${UNITY_PROJECT_PATH}/Assets/Plugins/Linux/x86_64")
    endif()
    set(UNITY_SCRIPTS_DIR "${UNITY_PROJECT_PATH}/Assets/Scripts/TinyEXR")
    set(UNITY_EXAMPLES_DIR "${UNITY_SCRIPTS_DIR}/Examples")

    add_custom_command(TARGET UnityTinyEXR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_PLATFORM_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_SCRIPTS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_EXAMPLES_DIR}"
        # Drop the old staged name so it cannot duplicate class UTinyEXRLoader.
        COMMAND ${CMAKE_COMMAND} -E remove -f "${UNITY_SCRIPTS_DIR}/TinyEXRLoader.cs"
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:UnityTinyEXR>
            "${UNITY_PLATFORM_DIR}/$<TARGET_FILE_NAME:UnityTinyEXR>"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CSHARP_SRC_DIR}/UTinyEXRLoader.cs"
            "${UNITY_SCRIPTS_DIR}/UTinyEXRLoader.cs"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CSHARP_SRC_DIR}/NativeExrBuffer.cs"
            "${UNITY_SCRIPTS_DIR}/NativeExrBuffer.cs"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CSHARP_SRC_DIR}/ExrRawImageData.cs"
            "${UNITY_SCRIPTS_DIR}/ExrRawImageData.cs"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${EXAMPLE_SCRIPT}"
            "${UNITY_EXAMPLES_DIR}/EXRLoaderExample.cs"
        COMMENT "Deploying to Unity: ${UNITY_PROJECT_PATH}/Assets"
    )

    message(STATUS "Unity deployment enabled: ${UNITY_PLATFORM_DIR}")
endif()

# Installation
install(TARGETS UnityTinyEXR
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)

# Print configuration
message(STATUS "----------------------------------------------")
message(STATUS "Unity Plugin Build Configuration")
message(STATUS "----------------------------------------------")
if(WIN32)
    message(STATUS "Output file: UnityTinyEXR.dll")
elseif(APPLE)
    message(STATUS "Output file: libUnityTinyEXR.bundle")
else()
    message(STATUS "Output file: libUnityTinyEXR.so")
endif()
message(STATUS "Build output: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "Package (dist): ${DIST_DIR}")
if(UNITY_PROJECT_PATH AND EXISTS "${UNITY_PROJECT_PATH}")
    message(STATUS "Unity deploy directory: ${UNITY_PLATFORM_DIR}")
endif()
message(STATUS "----------------------------------------------")
