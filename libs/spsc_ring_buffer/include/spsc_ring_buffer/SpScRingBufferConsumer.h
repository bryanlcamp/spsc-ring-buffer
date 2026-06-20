#pragma once

#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <type_traits>
#include <functional>

#include "spsc_ring_buffer/SpScRingBuffer.h"
#include "spsc_ring_buffer/Platform.h"

namespace spsc_ring_buffer {

inline constexpr size_t kConsumeBatchSize = 16; // Maximizes pipeline utilization rates.

/**
 * @brief Thread-isolated event consumer framework designed for low-latency batch sweeping.
 *
 * @tparam MsgType Underlying message type payload transferred through the ring buffer.
 * @tparam Callback Functional execution context triggered upon message consumption.
 * @tparam N Capacity boundary constraint mapped straight to the ring buffer target.
 */
template <typename MsgType, typename Callback, size_t N = DefaultCapacity>
class SpScRingBufferConsumer {
public:
    SpScRingBufferConsumer(SpScRingBuffer<MsgType, N> &buffer, Callback callback)
        : _buffer(buffer),
          _callback(std::move(callback)) {
    }

    ~SpScRingBufferConsumer() = default;

    // Copying is not allowed.
    SpScRingBufferConsumer(const SpScRingBufferConsumer&) = delete;
    SpScRingBufferConsumer& operator=(const SpScRingBufferConsumer&) = delete;

    /**
     * @brief Launches execution path and assigns dedicated hardware core affinities cleanly.
     * Uses zero heap allocations to preserve system page layout integrity.
     */
    void start(int coreId = -1) {
        _consumerThread = std::jthread([this, coreId](std::stop_token stopToken) { 
            // Lock executing engine thread context onto isolated architecture segment.
            spsc_ring_buffer::platform::pinCurrentThread(coreId);
            consumeLoopFused(stopToken); 
        });
    }

    /**
     * @brief Issues a cooperative stop request and flushes execution boundaries.
     */
    void stop() noexcept {
        _consumerThread.request_stop();
        if (_consumerThread.joinable()) {
            _consumerThread.join();
        }
    }

private:
    SpScRingBuffer<MsgType, N> &_buffer;
    Callback _callback;

    // Zero-allocation stack-allocated joint thread manager.
    std::jthread _consumerThread; 

    /**
     * @brief High-Speed Fused Batch Processing hot-path interface.
     */
    void consumeLoopFused(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) [[likely]] {
            // Drain blocks of sequential messages using std::ref to guarantee zero-copy lambda evaluation.
            size_t processed = _buffer.popBatch(std::ref(_callback), kConsumeBatchSize);

            if (processed == 0) [[unlikely]] {
                // Apply low-level architecture pause loop hints to eliminate pipeline stalls on dry paths.
                spsc_ring_buffer::platform::cpuPause(); 
            }
        }
    }
};

/**
 * @brief C++23 Explicit Class Template Argument Deduction (CTAD) Guide.
 * Completely eliminates the need for template parameter parsing helper factory methods.
 */
template <typename MsgType, size_t N, typename Callback>
SpScRingBufferConsumer(SpScRingBuffer<MsgType, N>&, Callback) 
    -> SpScRingBufferConsumer<MsgType, std::decay_t<Callback>, N>;

} // namespace spsc_ring_buffer