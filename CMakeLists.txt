cmake_minimum_required(VERSION 3.25)

# =========================================================================
# 1. RESOLVE MAC SYSTEM PATHS BEFORE THE FIRST project() TARGET CHECK
# =========================================================================
if(APPLE)
    # Dynamically extract your active Apple SDK location
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path 
        OUTPUT_VARIABLE MACOS_SDK_PATH 
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    # 2. Tell CMake to append the SDK path to the internal compiler checks
    set(CMAKE_OSX_SYSROOT ${MACOS_SDK_PATH} CACHE PATH "macOS SDK Root Path" FORCE)
    
    # 3. Tell your custom Homebrew GCC where the system libraries sit
    set(CMAKE_CXX_FLAGS_INIT "-isysroot ${MACOS_SDK_PATH}")
    set(CMAKE_C_FLAGS_INIT "-isysroot ${MACOS_SDK_PATH}")
endif()

# =========================================================================
# 2. INTRODUCE THE PROJECT RUNTIME
# =========================================================================
project(HftSuite LANGUAGES CXX)

# Enforce global C++23 standards across all operating systems
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Automatically generate tracking files to feed VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialize the global test suite tracking engines
option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    enable_testing()
endif()

# =========================================================================
# 3. SUBDIRECTORIES
# =========================================================================
# Routes through your central libs/ directory instead of sniping children directly
add_subdirectory(libs)
add_subdirectory(apps/CmeDecoder)

# Conditionally pull in your isolated testing framework
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()