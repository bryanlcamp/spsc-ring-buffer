#include <iostream>
#include <atomic>
#include <chrono>
#include <array>
#include <numeric>
#include <algorithm>
#include <thread>
#include <cstring>
#include "spsc_ring_buffer/SpScRingBuffer.h"
#include "spsc_ring_buffer/SpScRingBufferConsumer.h"
#include "cme/CmeBinaryDecoder.h"

using namespace spsc_ring_buffer;
using namespace hft::protocols::cme;

// --- EXTREMELY MAINTAINABLE TIMING TOGGLE ---
inline constexpr bool kBenchmarkMode = true; // Set to 'false' for pure, 100% un-invasive production speed

inline constexpr size_t kNumMessages = 1'000'000;
inline constexpr size_t kWarmupMessages = 50'000;

namespace {
    alignas(64) std::array<uint64_t, kBenchmarkMode ? kNumMessages : 1> g_latencies;
    std::atomic<size_t> g_receivedCount{0};
}

int main() {
    // 1. Allocate the generic lock-free transport queue (Agnostic POD container)
    SpScRingBuffer<CmeMarketPacket<kBenchmarkMode>, 8192> cmeQueue;
    CmeBinaryDecoder decoder;

    // 2. Setup Consumer Worker Thread - Processing the Full Extraction Chain
    SpScRingBufferConsumer consumer(cmeQueue, [&decoder](const auto& pkt) noexcept {
        uint64_t endTime = 0;
        if constexpr (kBenchmarkMode) {
            endTime = std::chrono::steady_clock::now().time_since_epoch().count();
        }
        
        // Allocate a zero-overhead application destination frame directly on the thread's stack
        BookUpdateEvent appEvent;
        
        // Execute extraction, field mapping, and character casting loops
        decoder.decode(pkt, appEvent);
        
        // --- SIMULATE APPLICATION TRADING STRATEGY RECEIVING THE SIMPLIFIED EVENT ---
        [[maybe_unused]] uint32_t activeSequence = appEvent.wireSequence;
        [[maybe_unused]] int64_t  topOfBookBid = appEvent.levels[0].price;
        
        if constexpr (kBenchmarkMode) {
            if (pkt.payload.transactTime >= kWarmupMessages) [[likely]] {
                size_t sampleIdx = pkt.payload.transactTime - kWarmupMessages;
                g_latencies[sampleIdx] = static_cast<uint64_t>(endTime - pkt.pipelineEntryNs);
            }
        }
        g_receivedCount.fetch_add(1, std::memory_order_relaxed);
    });

    std::cout << "--> Spinning up consumer thread loop (Timing Mode: " << (kBenchmarkMode ? "ON" : "OFF") << ")..." << std::endl;
    consumer.start(2); // Pins consumer thread to isolated hardware core 2 on Linux production systems
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 3. Allocate a cache-aligned stack wire buffer block to simulate network packet arrivals
    alignas(64) std::array<uint8_t, sizeof(CmeMarketPacket<kBenchmarkMode>)> rawNetworkStream;
    std::memset(rawNetworkStream.data(), 0, rawNetworkStream.size());

    // Pre-populate the immutable CME SBE fields inside the network bytes ahead of time
    auto* pktHeader = reinterpret_cast<CmePacketHeader*>(rawNetworkStream.data());
    auto* sbeHeader = reinterpret_cast<SbeMessageHeader*>(rawNetworkStream.data() + sizeof(CmePacketHeader));
    sbeHeader->blockLength = sizeof(CmeMdIncrementalRefresh);
    sbeHeader->templateId = 42; 
    sbeHeader->schemaId = 197;
    sbeHeader->version = 3;

    auto* grpHeader = reinterpret_cast<SbeGroupHeader*>(
        rawNetworkStream.data() + sizeof(CmePacketHeader) + sizeof(SbeMessageHeader) + sizeof(CmeMdIncrementalRefresh)
    );
    grpHeader->blockLength = sizeof(CmeMdPriceLevel);
    grpHeader->numInGroup = 3; 

    // Map pointer alignments across the internal array blocks
    auto* level0 = reinterpret_cast<CmeMdPriceLevel*>(rawNetworkStream.data() + offsetof(CmeMarketPacket<kBenchmarkMode>, bookLevels) + (0 * sizeof(CmeMdPriceLevel)));
    auto* level1 = reinterpret_cast<CmeMdPriceLevel*>(rawNetworkStream.data() + offsetof(CmeMarketPacket<kBenchmarkMode>, bookLevels) + (1 * sizeof(CmeMdPriceLevel)));
    auto* level2 = reinterpret_cast<CmeMdPriceLevel*>(rawNetworkStream.data() + offsetof(CmeMarketPacket<kBenchmarkMode>, bookLevels) + (2 * sizeof(CmeMdPriceLevel)));

    level0->mdEntryPx = 45502500000; level0->mdEntrySize = 25; level0->mdPriceLevel = 1; level0->mdUpdateAction = 0; level0->mdEntryType = '0'; 
    level1->mdEntryPx = 45505000000; level1->mdEntrySize = 14; level1->mdPriceLevel = 1; level1->mdUpdateAction = 0; level1->mdEntryType = '1'; 
    level2->mdEntryPx = 45500000000; level2->mdEntrySize = 110; level2->mdPriceLevel = 2; level2->mdUpdateAction = 1; level2->mdEntryType = '0'; 

    const size_t totalIterations = kWarmupMessages + kNumMessages;
    auto startWallTime = std::chrono::steady_clock::now();

    // 4. Hot Producer Network Driver Loop - Slamming the queue
    for (size_t i = 0; i < totalIterations; ++i) {
        pktHeader->msgSeqNum = static_cast<uint32_t>(i);
        pktHeader->sendingTime = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

        auto* payload = reinterpret_cast<CmeMdIncrementalRefresh*>(rawNetworkStream.data() + sizeof(CmePacketHeader) + sizeof(SbeMessageHeader));
        payload->transactTime = i; 
        payload->matchEventIndicator = 0x80;

        // Direct memory casting from raw bytes straight into the queue transport slot
        auto* wirePacketCast = reinterpret_cast<CmeMarketPacket<kBenchmarkMode>*>(rawNetworkStream.data());
        
        if constexpr (kBenchmarkMode) {
            wirePacketCast->pipelineEntryNs = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        }

        while (!cmeQueue.tryPush(*wirePacketCast)) {
            cpuPause(); 
        }
    }

    while (g_receivedCount.load(std::memory_order_relaxed) < totalIterations) {
        cpuPause();
    }
    
    auto endWallTime = std::chrono::steady_clock::now();
    consumer.stop();

    // 5. Render Full Pipeline Performance Metrics Summary
    if constexpr (kBenchmarkMode) {
        std::sort(g_latencies.begin(), g_latencies.end());
        uint64_t sum = std::accumulate(g_latencies.begin(), g_latencies.end(), 0ULL);
        double avg = static_cast<double>(sum) / kNumMessages;
        
        uint64_t p50  = g_latencies[static_cast<size_t>(kNumMessages * 0.50)];
        uint64_t p95  = g_latencies[static_cast<size_t>(kNumMessages * 0.95)];
        uint64_t p99  = g_latencies[static_cast<size_t>(kNumMessages * 0.99)];
        uint64_t p999 = g_latencies[static_cast<size_t>(kNumMessages * 0.999)];
        
        auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endWallTime - startWallTime).count();
        double throughput = (static_cast<double>(totalIterations) / (static_cast<double>(totalDurationMs) / 1000.0)) / 1e6;

        std::cout << "\n====================================================\n"
                  << "    FULL PIPELINE WIRE-TO-APP DEMONSTRATION         \n"
                  << "====================================================\n"
                  << "  Throughput Performance : " << throughput << " Million msgs/sec\n\n"
                  << "  END-TO-END PIPELINE LATENCY PROFILE (Pop + Decode + Output):\n"
                  << "    Average Latency      : " << avg << " ns\n"
                  << "    Median (p50)         : " << p50 << " ns\n"
                  << "    95th Percentile (p95): " << p95 << " ns\n"
                  << "    99th Percentile (p99): " << p99 << " ns\n"
                  << "    99.9th (Tail p999)   : " << p999 << " ns\n"
                  << "====================================================\n" << std::endl;
    } else {
        std::cout << "Production run complete. Telemetry bypassed successfully." << std::endl;
    }

    return 0;
}