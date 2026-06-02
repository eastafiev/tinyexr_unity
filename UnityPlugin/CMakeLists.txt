# Unity TinyEXR Plugin CMake Configuration
cmake_minimum_required(VERSION 3.5)
project(UnityTinyEXRPlugin VERSION 1.0.0 LANGUAGES C CXX)

# C++11 standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(USE_MINIZ "Use miniz instead of system zlib" ON)
set(UNITY_PROJECT_PATH "" CACHE PATH "Path to Unity project for deployment")

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
    $<$<BOOL:${USE_MINIZ}>:TINYEXR_USE_MINIZ=1>
)

if(MSVC)
    target_compile_definitions(UnityTinyEXR PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

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

# Unity deployment
if(UNITY_PROJECT_PATH AND EXISTS "${UNITY_PROJECT_PATH}")
    # Determine platform subdirectory
    if(WIN32)
        set(UNITY_PLATFORM_DIR "${UNITY_PROJECT_PATH}/Assets/Plugins/x86_64")
    elseif(APPLE)
        set(UNITY_PLATFORM_DIR "${UNITY_PROJECT_PATH}/Assets/Plugins/macOS")
    else()
        set(UNITY_PLATFORM_DIR "${UNITY_PROJECT_PATH}/Assets/Plugins/Linux/x86_64")
    endif()
    
    # Create directory
    file(MAKE_DIRECTORY "${UNITY_PLATFORM_DIR}")
    
    # Copy plugin after build
    add_custom_command(TARGET UnityTinyEXR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:UnityTinyEXR>
            "${UNITY_PLATFORM_DIR}/$<TARGET_FILE_NAME:UnityTinyEXR>"
        COMMENT "Deploying plugin to Unity: ${UNITY_PLATFORM_DIR}"
    )
    
    # Copy C# wrapper
    set(CSHARP_FILES
        csharp/TinyEXRLoader.cs
    )
    
    set(UNITY_SCRIPTS_DIR "${UNITY_PROJECT_PATH}/Assets/Scripts/TinyEXR")
    file(MAKE_DIRECTORY "${UNITY_SCRIPTS_DIR}")
    
    foreach(CS_FILE ${CSHARP_FILES})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${CS_FILE}")
            get_filename_component(CS_NAME ${CS_FILE} NAME)
            add_custom_command(TARGET UnityTinyEXR POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${CMAKE_CURRENT_SOURCE_DIR}/${CS_FILE}"
                    "${UNITY_SCRIPTS_DIR}/${CS_NAME}"
                COMMENT "Deploying ${CS_NAME} to Unity"
            )
        endif()
    endforeach()
    
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
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
if(UNITY_PROJECT_PATH AND EXISTS "${UNITY_PROJECT_PATH}")
    message(STATUS "Unity deploy directory: ${UNITY_PLATFORM_DIR}")
endif()
message(STATUS "----------------------------------------------")
