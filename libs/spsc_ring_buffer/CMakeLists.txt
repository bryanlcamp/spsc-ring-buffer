cmake_minimum_required(VERSION 3.25)

add_library(spsc_ring_buffer INTERFACE)

target_include_directories(spsc_ring_buffer INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(spsc_ring_buffer INTERFACE cxx_std_23)