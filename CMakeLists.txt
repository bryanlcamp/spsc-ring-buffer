cmake_minimum_required(VERSION 3.25)
project(HftSuite LANGUAGES CXX)

# 1. Enforce global C++23 standards across all operating systems
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Automatically generate tracking files to feed VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 2. Adaptive System Self-Healing Configurations
if(APPLE)
    # macOS Specific: Dynamically pull the hidden Apple SDK path to resolve libSystem links
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path 
        OUTPUT_VARIABLE MACOS_SDK_PATH 
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_OSX_SYSROOT ${MACOS_SDK_PATH} CACHE PATH "macOS SDK Root Path" FORCE)
    
    # Securely target your custom manually installed GCC binary
    set(CMAKE_CXX_COMPILER "/opt/homebrew/bin/g++-15")

elseif(UNIX AND NOT APPLE)
    # Linux Specific: Fall back directly to the native Linux standard GNU toolchain
    set(CMAKE_CXX_COMPILER "g++")

elseif(WIN32)
    # Windows Specific: MSVC requires explicit flags to unlock complete C++23 specifications
    add_compile_options(/std:c++latest /permissive-)
endif()

# 3. Discover and register modular subdirectories in dependency order
add_subdirectory(libs/protocols)
add_subdirectory(libs/spsc_ring_buffer)
add_subdirectory(apps/CmeDecoder)
add_subdirectory(apps/DropCopyLogger)