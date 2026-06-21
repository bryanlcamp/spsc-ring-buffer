# Low-Latency C++23 SPSC Lock-Free Ring Buffer
A low-latency, single-producer single-consumer (SPSC) ring buffer, written in C++ 23. Suitable for high frequency trading (HFT), or any project where nanseconds are critical. Performs CPU core optimizations on modern x86_64 and ARM64 architectures,  including thread-pinning, affinity, and maximizing each core's L1/L2 cache. It uses immediate functions (constexpr, consteval, etc.) where appropriate while doing its best to avoid cache misses, as well as branch prediction to maintain each core's history and to keep the CPU from stalling. 

## Performance Profiles (Apple M2 (2022) Macbook Air)
The following metrics were measured on an Apple M2 (2022) Macbook Air. Consequently, I was not able to take advantage of many of the core optimizations listed above. I have tickets to perform these same checks on Linux and Windows. In a production environment, one would likely be using server-grade hardware. Note that this solution is <u><b>software-only</b></u>: it assumes kernel-bypass, and FPGA, are not available. <br><br>

## <b>Inputs:</b><br>
<b>CmeDecoder Metrics: End-To-End.</b><br>.
- A total of 1,000,000 CME SBE messages.
- Inserting raw, binary messages into the SPSC ring buffer.
- Popping raw, binary message off the SPSC ring buffer.
- Deserialing the CME SBE message on the consumer thread.
- Creating a much smaller, app-friendly data-structure for each message.
- Capturing the metrics.<br><br>
## <b>Outputs:</b><br>
- **Avg Latency**: 168.272 ns<br>
- **p50 (Median)**: 0 ns *(Tick resolution < bounds of the OS's steady tracking clock)*<br>
- **p95**:   1'000 ns<br>
- **p99**:   3'000 ns<br>
- **99.9**: 11'000 ns<br><br>

## <b>Stand-Alone Deployment:</b><br>
This ring buffer is designed to be 100% independent. The only files needed to copy and reuse are in the spsc_ring_buffer/include directory.

## <b>Prerequisites:</b><br>
- **Toolchain**: A compiler that supports C++ 23 (`g++ >= 13` or `clang++ >= 17`).
- **Build System**: CMake version 3.25 or higher.

## <b>Executing the examples:<b><br>
To compile and execute, use ./build.sh in the project's rood directory. E.g.,<br> 
- ./build.sh clean
- ./build.sh init
- ./build.sh CmeDecoder
