cmake_minimum_required(VERSION 3.25)
project(spsc_ring_buffer_tests LANGUAGES CXX)

# =========================================================================
# 1. INITIALIZE TESTING ENGINE
# =========================================================================
enable_testing()

# Prevent GoogleTest from overriding the compiler's runtime choices
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# Prevent GoogleTest binaries from spilling into project installation paths
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# =========================================================================
# 2. ADD THE OFFLINE GOOGLETEST ENGINE
# =========================================================================
# Points directly to the clean tests/googletest folder
add_subdirectory(googletest)

# =========================================================================
# 3. BUILD THE TEST SUITE BINARY
# =========================================================================
add_executable(unit_tests
    test_ring_buffer.cpp
)

# Link GoogleTest and your completely independent ring buffer headers
target_link_libraries(unit_tests PRIVATE
    gtest_main
    spsc_ring_buffer
)

# =========================================================================
# 4. CTEST AUTOMATION DRIVER
# =========================================================================
include(GoogleTest)
gtest_discover_tests(unit_tests)