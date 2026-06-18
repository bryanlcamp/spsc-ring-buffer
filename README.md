# Low-Latency C++23 SPSC Lock-Free Ring Buffer
A low-latency, single-producer single-consumer (SPSC) queue, written in C++ 23. Suitable for high frequency trading (HFT), or any project where nanseconds are critical. Performs CPU core optimizations on modern x86_64 and ARM64 architectures,  including thread-pinning, affinity, and maximizing each core's L1/L2 cache. It uses immediate functions (constexpr, consteval, etc.) where appropriate while doing its best to avoid cache misses. Needless to say, virtual functions are avoided at all costs. It uses branch prediction to maintain each core's history and to keep the CPU from stalling. 

## Performance Profiles (Apple M2 (2022) Macbook Air)
The following metrics were measured on an Apple M2 (2022) Macbook Air. Consequently, I was not able to take advantage of many of the core optimizations listed above. I have performed these checks on Linux, but do not have the complete set of metrics. This is on my TODO list. Also, in a production environment, one would likely be using server-grade hardware. Note that this solution is <u><b>software-only</b></u>: it assumes kernel-bypass, and FPGA, are not available. <br><br>

## <b>Metrics:</b><br>
<b>Inputs:</b><br>
- 26.5 msg/sec
- Note: The metrics above include the following:
- Receipt of a CME MBE message.
- Inserting the raw, binary message onto onto the SPSC queue.
- Popping the raw, binary message off of the SPSC queuue.
- Deserialing the CME message.
- Inserting fields from the message into a much smaller, app-friendly data-structure.<br><br>
<b>Metrics:</b><br><br>
- **Avg Latency**: 562.555 ns<br>
- **p50 (Median)**: 0 ns *(Tick resolution < bounds of the OS's steady tracking clock)*<br>
- **p95**: 2,000 ns<br>
- **p99**: 13,000 ns<br>
- **99.9**: 36,000 ns<br><br>

### 2. Outbound Offloading Pipeline (`DropCopyLogger`)
- **Average Time Per Transfer**: 32.499 ns/msg
- **Throughput Performance**: 31.25 Million reports/sec

## System Mechanics: Architectural Breakdown

To accurately evaluate the telemetry output, it is critical to differentiate between the **Raw Transport Cost** (~32ns) and the **Full Protocol Translation Cost** (~562ns):

### The Outbound Flow (`DropCopyLogger` | ~32.499 ns)
This benchmark measures the **pure, isolated software transport latency** of the queue itself across thread boundaries. At a typical CPU clock speed of 3.2 GHz to 3.4 GHz, 32 nanoseconds represents a micro-budget of roughly **100 to 110 hardware clock cycles** per message. 
- **What it is doing**: It measures a straight memory-to-memory copy of a flat, packed 26-byte `ExecutionReport` payload. The producer core instantiates the struct, executes a branchless bitwise AND index calculation, copies the payload into the slot, and performs a release store. The consumer core coordinates an acquire load, reads the slot, and advances pointers.
- **The Core Interconnect Limit**: The 32ns boundary represents the raw physical hardware speed limit of the CPU's internal L1/L2 memory fabric shifting cache lines back and forth via cache coherency invalidation protocols under a maximum capacity load.
- **Hot-Path Handoff Optimization**: Because the consumer loop executes on an entirely separate background core, the trading thread only pays the immediate `tryPush()` instruction cost (roughly **10 to 12 nanoseconds**) before instantly resuming market operations. Heavy text formatting and disk I/O are hidden entirely off the critical path.

### The Inbound Flow (`CmeDecoder` | ~562.555 ns)
This benchmark measures the **Raw Transport Cost PLUS the Full Protocol Processing Loop Cost**. It represents a true production-representative electronic trading pipeline.
- **What it is doing**: The consumer core pops a generic binary chunk from the queue and dispatches it directly to a decoupled CME MDP 3.0 protocol library. The decoder un-packs the complex binary wire format, reads the `CmePacketHeader`, parses the binary `SbeMessageHeader`, walks a variable-length `SbeGroupHeader` block, sequentially processes 3 nested price/volume depth layers, branchlessly translates exchange character actions/side tags into clean application primitives, and outputs a simplified `BookUpdateEvent` onto the thread's stack.

## Core Architectural Design

- **Natural Monotonic Sequence Overflow**: Head and tail utilize raw 64-bit continuous unsigned integers. Index management completely avoids boundaries, branches, or modulus operators (`%`), naturally resolving queue full vs. empty ambiguity via branchless rollover comparisons.
- **Array Index Mask Fusion**: Bitwise masks (`index & IndexMask`) are applied exclusively at the point of array lookups. The compiler collapses this pointer arithmetic down to a single instruction offset lookup, unburdening hot loop registers.
- **Cache Line Isolation**: Producer writes (`_head`) and consumer writes (`_tail`) are hard-isolated on distinct cache blocks using C++23 implementation-defined parameters (such as `std::hardware_destructive_interference_size`), completely eliminating false sharing and core invalidation bouncing.
- **C++23 Explicit Object Parameters**: Leveraging explicit object parameters ("deducing this") inside element peek inspection templates squashes duplicate const/non-const code definitions, ensuring optimal CPU L1 instruction cache residency.
- **Non-Invasive Compile-Time Telemetry**: Latency tracking is evaluated via `if constexpr` parameters. Setting `kBenchmarkMode = false` instructs the compiler's optimization pass to remove every tracking variable and clock polling command, generating an un-polluted, zero-footprint production binary.

## Stand-Alone Deployment: Using the Queue Natively
This queue is designed to be 100% independent. The only files necessary to copy are in the include directory.


### Prerequisite Environment
- **Toolchain**: A strict C++23 compliant compiler (`g++ >= 13` or `clang++ >= 17`).
- **Build System**: CMake version 3.25 or higher.

### Executing the Simulators
To compile and execute, use ./build.sh in the project's rood directory. E.g., 
./build.sh clean
./build.sh init
./build.sh DropCopyLogger

### Production Compilation Rule Constraints
When building this repository or lifting the stand-alone headers into a production environment, you must ensure that Link-Time Optimization (LTO) and host microarchitecture targeting flags (`-O3 -march=native -flto`) are fully engaged by your compiler. This allows the compiler's loop optimization passes to fully flatten the layout boundaries, inline the pointer mapping methods, and eliminate dead safety branches.
