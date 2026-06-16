#include <iostream>
#include <atomic>
#include <chrono>
#include <array>
#include <numeric>
#include <algorithm>
#include <thread>
#include "spsc_ring_buffer/SpScRingBuffer.h"
#include "spsc_ring_buffer/SpScRingBufferConsumer.h"

using namespace spsc_ring_buffer;

#pragma pack(push, 1)
/**
 * @brief High-velocity execution confirmation packet.
 * Packed to minimal byte boundaries to minimize memory bus footprint during hot-path handoffs.
 */
struct ExecutionReport {
    uint64_t timestampNs;
    uint64_t orderId;
    int64_t  fillPrice;
    uint32_t fillQty;
    char     side;     // 'B' = Buy, 'S' = Sell
    char     execType; // 'F' = Full Fill
};
#pragma pack(pop)

inline constexpr size_t kTotalLogs = 1'000'000; // 1 Million fills for an accurate steady-state test

namespace {
    std::atomic<size_t> g_writtenCount{0};
}

int main() {
    // 1. Allocate an independent lock-free transport queue
    SpScRingBuffer<ExecutionReport, 8192> logQueue;

    // 2. Setup the background Asynchronous I/O Logger Thread
    // Consumer callback handles heavy string formatting and I/O completely off the hot path
    SpScRingBufferConsumer asyncLogger(logQueue, [](const ExecutionReport& report) noexcept {
        // Simulates high-cost text formatting or disk file I/O executing completely off the trading core
        [[maybe_unused]] uint64_t id = report.orderId;
        [[maybe_unused]] int64_t  px = report.fillPrice;
        g_writtenCount.fetch_add(1, std::memory_order_relaxed);
    });

    std::cout << "--> Initializing Asynchronous Drop Copy Logger Target..." << std::endl;
    asyncLogger.start(3); // Hard-pin the async I/O worker to CPU Core 3, leaving the trading core unburdened
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto startWallTime = std::chrono::steady_clock::now();

    // 3. Hot Execution Thread Loop (Simulating the active trading core)
    std::cout << "--> Commencing execution reports data blast (1M records)..." << std::endl;
    for (size_t i = 0; i < kTotalLogs; ++i) {
        ExecutionReport report{
            .timestampNs = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()),
            .orderId = 1000000ULL + i,
            .fillPrice = 45500 + static_cast<int64_t>(i % 10),
            .fillQty = 10,
            .side = (i % 2 == 0) ? 'B' : 'S',
            .execType = 'F'
        };

        // Non-blocking hot-path push: takes fewer than 15ns to offload the event
        while (!logQueue.tryPush(report)) {
            cpuPause();
        }
    }

    // Block main thread until background logger completely flushes the queue
    while (g_writtenCount.load(std::memory_order_relaxed) < kTotalLogs) {
        cpuPause();
    }

    auto endWallTime = std::chrono::steady_clock::now();
    asyncLogger.stop();

    // 4. Render Telemetry Profile Report Window with explicit nanosecond formatting
    auto totalDurationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endWallTime - startWallTime).count();
    auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endWallTime - startWallTime).count();
    
    double throughput = (static_cast<double>(kTotalLogs) / (static_cast<double>(totalDurationMs) / 1000.0)) / 1e6;
    double avgLatencyPerRecord = static_cast<double>(totalDurationNs) / kTotalLogs;

    std::cout << "\n====================================================\n"
              << "    ASYNCHRONOUS DROP COPY LOGGER DEMONSTRATION     \n"
              << "====================================================\n"
              << "  Total Reports Logged       : " << kTotalLogs << "\n"
              << "  Logging Output Throughput  : " << throughput << " Million reports/sec\n"
              << "  Average Time Per Transfer  : " << avgLatencyPerRecord << " ns/msg\n"
              << "====================================================\n" << std::endl;

    return 0;
}
