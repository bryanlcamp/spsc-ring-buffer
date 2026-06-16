add_library(spsc_ring_buffer INTERFACE)

# Expose the root include directory directly
target_include_directories(spsc_ring_buffer INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Keep the link to the standalone ring buffer secure
target_link_libraries(spsc_ring_buffer INTERFACE spsc_ring_buffer)