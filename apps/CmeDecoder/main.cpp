#include <iostream>
#include <atomic>
#include <chrono>
#include <array>
#include <numeric>
#include <algorithm>
#include <thread>
#include <cstring>

// TODO: fix these paths.
#include "../../libs/spsc_ring_buffer/include/spsc_ring_buffer/SpScRingBuffer.h"
#include "../../libs/spsc_ring_buffer/include/spsc_ring_buffer/SpScRingBufferConsumer.h"
#include "cme/CmeBinaryDecoder.h"

using namespace spsc_ring_buffer;
using namespace hft::protocols::cme;

// --- EXTREMELY MAINTAINABLE TIMING TOGGLE ---
inline constexpr bool kBenchmarkMode = true; // Set to 'false' for pure, 100% un-invasive production speed

inline constexpr size_t kNumMessages = 1'000'000;
inline constexpr size_t kWarmupMessages = 50'000;
inline constexpr size_t kWarmupMilliseconds = 50;

namespace {
    // Ugly, but this is an example.
    alignas(64) std::array<uint64_t, kBenchmarkMode ? kNumMessages : 1> g_latencies;
    std::atomic<size_t> g_receivedCount{0};
}

int main() {
    // Allocate the queue with benchmark mode enabled.
    SpScRingBuffer<CmeMarketPacket<kBenchmarkMode>, 8192> cmeQueue;

    // Allocate the CME MBE binary packet decoder.
    CmeBinaryDecoder decoder;

    // Start the queue's consumer thread.
    SpScRingBufferConsumer consumer(cmeQueue, [&decoder](const auto& packet) noexcept {
      uint64_t endTime = 0;
      if constexpr (kBenchmarkMode) {
        endTime = std::chrono::steady_clock::now().time_since_epoch().count();
      }
        
      // Turn the CME SBE packet into our own data structure.
      // Note: we're on the consumer's thread.
      BookUpdateEvent appEvent;
      decoder.decode(packet, appEvent);
      [[maybe_unused]] uint32_t activeSequence = appEvent.wireSequence;
      [[maybe_unused]] int64_t  topOfBookBid = appEvent.levels[0].price;
        
      if constexpr (kBenchmarkMode) {
        if (packet.payload.transactTime >= kWarmupMessages) [[likely]] {
          size_t sampleIdx = packet.payload.transactTime - kWarmupMessages;
          g_latencies[sampleIdx] = static_cast<uint64_t>(endTime - packet.pipelineEntryNs);
        }
      }  
      
      // End CmeBinaryDecoder processing

      g_receivedCount.fetch_add(1, std::memory_order_relaxed);
    });

    // Start the consumer the thread (pin to core 2 on Linux/Windows).
    consumer.start(2);

    // Allow the producer some time to warm up.
    std::this_thread::sleep_for(std::chrono::milliseconds(kWarmupMilliseconds));

    // Allocate a cache-aligned buffer to simulate CME packet arrivals.
    // We could also do this via UDP Multicast if necessary.
    alignas(64) std::array<uint8_t, sizeof(CmeMarketPacket<kBenchmarkMode>)> rawNetworkStream;
    std::memset(rawNetworkStream.data(), 0, rawNetworkStream.size());

    // Initialize CME/SBE packet/message/group headers.
    auto* packetHeader = reinterpret_cast<CmePacketHeader*>(rawNetworkStream.data());
    auto* sbeMessageHeader = reinterpret_cast<SbeMessageHeader*>(rawNetworkStream.data() + 
      sizeof(CmePacketHeader));
    sbeMessageHeader->blockLength = sizeof(CmeMdIncrementalRefresh);
    sbeMessageHeader->templateId = 42; 
    sbeMessageHeader->schemaId   = 197;
    sbeMessageHeader->version    = 3;
    auto* groupHeader = reinterpret_cast<SbeGroupHeader*>(
      rawNetworkStream.data()  + 
      sizeof(CmePacketHeader)  + 
      sizeof(SbeMessageHeader) + 
      sizeof(CmeMdIncrementalRefresh)
    );
    groupHeader->blockLength = sizeof(CmeMdPriceLevel);
    groupHeader->numInGroup  = 3; 

    // Map pointer alignments.
    auto* level0 = reinterpret_cast<CmeMdPriceLevel*>(rawNetworkStream.data() + 
      offsetof(CmeMarketPacket<kBenchmarkMode>, bookLevels) + 
      (0 * sizeof(CmeMdPriceLevel)));
    auto* level1 = reinterpret_cast<CmeMdPriceLevel*>(rawNetworkStream.data() + 
      offsetof(CmeMarketPacket<kBenchmarkMode>, bookLevels) + 
      (1 * sizeof(CmeMdPriceLevel)));
    auto* level2 = reinterpret_cast<CmeMdPriceLevel*>(rawNetworkStream.data() + 
      offsetof(CmeMarketPacket<kBenchmarkMode>, bookLevels) + 
      (2 * sizeof(CmeMdPriceLevel)));

    level0->mdEntryPx = 45502500000; 
    level0->mdEntrySize = 25; 
    level0->mdPriceLevel = 1; 
    level0->mdUpdateAction = 0; 
    level0->mdEntryType = '0';

    level1->mdEntryPx = 45505000000; 
    level1->mdEntrySize = 14; 
    level1->mdPriceLevel = 1; 
    level1->mdUpdateAction = 0; 
    level1->mdEntryType = '1';

    level2->mdEntryPx = 45500000000;
    level2->mdEntrySize = 110;
    level2->mdPriceLevel = 2;
    level2->mdUpdateAction = 1;
    level2->mdEntryType = '0'; 

    const size_t totalIterations = kWarmupMessages + kNumMessages;

    // Benchmarking.
    auto startWallTime = std::chrono::steady_clock::now();

    // Slam the queue.
    for (size_t i = 0; i < totalIterations; ++i) {
      packetHeader->msgSeqNum = static_cast<uint32_t>(i);
      packetHeader->sendingTime = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

      auto* payload = 
        reinterpret_cast<CmeMdIncrementalRefresh*>(rawNetworkStream.data() + 
          sizeof(CmePacketHeader) + 
          sizeof(SbeMessageHeader));
      payload->transactTime = i; 
      payload->matchEventIndicator = 0x80;

      // Direct memory casting from raw bytes straight into the queue transport slot
      auto* wirePacketCast = 
        reinterpret_cast<CmeMarketPacket<kBenchmarkMode>*>(rawNetworkStream.data());
        
      if constexpr (kBenchmarkMode) {
        wirePacketCast->pipelineEntryNs = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
      }

      while (!cmeQueue.tryPush(*wirePacketCast)) {
        // Back off if the queue is full.
        spsc_ring_buffer::platform::cpuPause(); 
      }
    }

    // Wait on the consumer to drain the queue.
    while (g_receivedCount.load(std::memory_order_relaxed) < totalIterations) {
        spsc_ring_buffer::platform::cpuPause();
    }
    auto endWallTime = std::chrono::steady_clock::now();
    consumer.stop();

    // Print our benchmarks.
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
    } 
    else {
        std::cout << "Production run complete. No statistics gathered." << std::endl;
    }

    return 0;
}