# Low-Latency C++23 SPSC Lock-Free Ring Buffer

A header-only, zero-dependency, single-producer single-consumer (SPSC) circular queue optimized for high-frequency trading (HFT) workflows. This module is engineered under strict C++23 tracking specifications to achieve maximum throughput and deterministic core-to-core data transport on modern x86_64 and ARM64 architectures.

## Performance Profiles (MacBook Air Benchmark Results)

The following metrics represent real-world multi-threaded execution patterns running natively on stock hardware without kernel bypass or specialized hardware assistance.

### 1. Inbound Ingestion Pipeline (`CmeExample`)
- **Average Latency**: 562.555 ns
- **Median (p50)**: 0 ns *(Indicates processing completion drops below the minimum tick resolution bounds of the operating system's steady tracking clock)*
- **95th Percentile (p95)**: 2,000 ns
- **99th Percentile (p99)**: 13,000 ns
- **99.9th Percentile (p99.9)**: 36,000 ns
- **Throughput Performance**: 21 Million msgs/sec

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

### The Inbound Flow (`CmeExample` | ~562.555 ns)
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
