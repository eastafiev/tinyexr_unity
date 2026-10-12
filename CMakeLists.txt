# Root CMake: build UnityTinyEXR native plugin and stage Assets-shaped dist/ package.
cmake_minimum_required(VERSION 3.5)
project(UnityTinyEXRPlugin VERSION 1.0.0 LANGUAGES C CXX)

# C++11 standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(USE_MINIZ "Use miniz instead of system zlib" ON)
set(UNITY_PROJECT_PATH "" CACHE PATH "Path to Unity project for deployment")
# Final package folder at repo root (Plugins + Scripts for drop-into Unity Assets)
set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist" CACHE PATH "Unity Assets-shaped package output directory")

# Source files
set(PLUGIN_SOURCES
    UnityPlugin/native/unity_tinyexr_plugin.cpp
)

set(PLUGIN_HEADERS
    UnityPlugin/native/unity_tinyexr_plugin.h
    tinyexr.h
)

# Compression library
if(USE_MINIZ)
    set(MINIZ_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz/miniz.c")
    if(EXISTS "${MINIZ_PATH}")
        list(APPEND PLUGIN_SOURCES ${MINIZ_PATH})
        message(STATUS "Using miniz from ${MINIZ_PATH}")
    else()
        message(WARNING "miniz.c not found at ${MINIZ_PATH}, using system zlib")
        set(USE_MINIZ OFF)
    endif()
endif()

if(NOT USE_MINIZ)
    find_package(ZLIB REQUIRED)
    message(STATUS "Using system zlib: ${ZLIB_LIBRARIES}")
endif()

# Create DLL
add_library(UnityTinyEXR SHARED ${PLUGIN_SOURCES})

# Includes
target_include_directories(UnityTinyEXR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UnityPlugin/native
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/miniz
)

# Link
if(NOT USE_MINIZ)
    target_link_libraries(UnityTinyEXR PRIVATE ZLIB::ZLIB)
endif()

# Definitions
target_compile_definitions(UnityTinyEXR PRIVATE
    UNITY_TINYEXR_EXPORTS=1
    $<$<BOOL:${USE_MINIZ}>:TINYEXR_USE_MINIZ=1>
)

if(MSVC)
    target_compile_definitions(UnityTinyEXR PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# Platform-specific
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
    )
else()
    set_target_properties(UnityTinyEXR PROPERTIES
        OUTPUT_NAME "UnityTinyEXR"
        PREFIX "lib"
        SUFFIX ".so"
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Output directory
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
set(CSHARP_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/UnityPlugin/csharp/TinyEXRLoader.cs")

# Always stage DLL + C# into dist/ after build
add_custom_command(TARGET UnityTinyEXR POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_PLATFORM_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_SCRIPTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:UnityTinyEXR>
        "${DIST_PLATFORM_DIR}/$<TARGET_FILE_NAME:UnityTinyEXR>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CSHARP_WRAPPER}"
        "${DIST_SCRIPTS_DIR}/TinyEXRLoader.cs"
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

    add_custom_command(TARGET UnityTinyEXR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_PLATFORM_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_SCRIPTS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:UnityTinyEXR>
            "${UNITY_PLATFORM_DIR}/$<TARGET_FILE_NAME:UnityTinyEXR>"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CSHARP_WRAPPER}"
            "${UNITY_SCRIPTS_DIR}/TinyEXRLoader.cs"
        COMMENT "Deploying to Unity: ${UNITY_PROJECT_PATH}/Assets"
    )

    message(STATUS "Unity deployment: ${UNITY_PLATFORM_DIR}")
endif()

# Print info
message(STATUS "==========================================")
message(STATUS "Unity TinyEXR Plugin Configuration")
message(STATUS "==========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build output: ${CMAKE_BINARY_DIR}/bin")
message(STATUS "Package (dist): ${DIST_DIR}")
if(UNITY_PROJECT_PATH AND EXISTS "${UNITY_PROJECT_PATH}")
    message(STATUS "Unity deploy: ${UNITY_PLATFORM_DIR}")
endif()
message(STATUS "==========================================")
