add_library(proto_cme INTERFACE)

# Expose the root include directory directly
target_include_directories(proto_cme INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Keep the link to the standalone ring buffer secure
target_link_libraries(proto_cme INTERFACE spsc_ring_buffer)