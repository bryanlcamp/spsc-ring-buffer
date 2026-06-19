cmake_minimum_required(VERSION 3.25)

# Initialize your market data decoding application target
add_executable(CmeDecoder main.cpp)

# Link your protocol target (which automatically down-propagates the ring buffer headers)
target_link_libraries(CmeDecoder PRIVATE proto_cme)
