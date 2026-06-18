# Low-Latency C++23 SPSC Lock-Free Ring Buffer
A low-latency, single-producer single-consumer (SPSC) queue, written in C++ 23. Suitable for high frequency trading (HFT), or any project where nanseconds are critical. Performs CPU core optimizations on modern x86_64 and ARM64 architectures,  including thread-pinning, affinity, and maximizing each core's L1/L2 cache. It uses immediate functions (constexpr, consteval, etc.) where appropriate while doing its best to avoid cache misses. Needless to say, virtual functions are avoided at all costs. It uses branch prediction to maintain each core's history and to keep the CPU from stalling. 

## Performance Profiles (Apple M2 (2022) Macbook Air)
The following metrics were measured on an Apple M2 (2022) Macbook Air. Consequently, I was not able to take advantage of many of the core optimizations listed above. I have performed these checks on Linux, but do not have the complete set of metrics. This is on my TODO list. Also, in a production environment, one would likely be using server-grade hardware. Note that this solution is <u><b>software-only</b></u>: it assumes kernel-bypass, and FPGA, are not available. <br><br>

## <b>Inputs:</b><br>
<b>Messages:</b><br>
- 26.5 msg/sec
- Note: The metrics above include the following:
- Receipt of a CME MBE message.
- Inserting the raw, binary message onto onto the SPSC queue.
- Popping the raw, binary message off of the SPSC queuue.
- Deserialing the CME message.
- Inserting fields from the message into a much smaller, app-friendly data-structure.<br><br>
## <b>Outputs:</b><br>
<b>Performance:</b><br>
- **Avg Latency**: 562.555 ns<br>
- **p50 (Median)**: 0 ns *(Tick resolution < bounds of the OS's steady tracking clock)*<br>
- **p95**: 2,000 ns<br>
- **p99**: 13,000 ns<br>
- **99.9**: 36,000 ns<br><br>

## <b>Stand-Alone Deployment:</b><br>
This queue is designed to be 100% independent. The only files necessary to copy are in the include directory.

## <b>Prerequisites:</b><br>
- **Toolchain**: A compiler that supports C++ 23 (`g++ >= 13` or `clang++ >= 17`).
- **Build System**: CMake version 3.25 or higher.

### Executing the examples:
To compile and execute, use ./build.sh in the project's rood directory. E.g., 
./build.sh clean
./build.sh init
./build.sh CmeDecoder
