#pragma once

#include <utility>
#include <memory>
#include <type_traits>
#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>

#include "spsc_ring_buffer/Platform.h"

namespace spsc_ring_buffer {

inline constexpr size_t   DefaultRingBufferCapacity = 1024;
inline constexpr uint64_t SpinCheckInterval = 1024;
inline constexpr uint64_t SpinCheckMask = SpinCheckInterval - 1;
inline constexpr uint32_t DefaultPushTimeoutMs = 5000;

/**
 * @brief Thread-isolated, zero-allocation lock-free single-producer single-consumer circular queue.
 *
 * @tparam T Core message type payload. Does NOT require default constructibility.
 * @tparam Capacity Slot allocation boundary. MUST represent an explicit power of two.
 */
template <typename T, size_t Capacity = DefaultRingBufferCapacity>
class SpScRingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of 2");

public:
  SpScRingBuffer() : _head(0), _tail(0), _dropped(0), _peakOccupancy(0) {}

  /**
   * @brief Destructor guarantees proper type-erasure stack unwinding.
   */
  ~SpScRingBuffer() noexcept {
      T dummy;
      while (tryPop(dummy)); 
  }

  // Enforce structural non-copyable/non-movable properties to maintain cache line layout invariants
  SpScRingBuffer(const SpScRingBuffer&) = delete;
  SpScRingBuffer& operator=(const SpScRingBuffer&) = delete;
  SpScRingBuffer(SpScRingBuffer&&) = delete;
  SpScRingBuffer& operator=(SpScRingBuffer&&) = delete;

  /**
   * @brief Single Message Enqueue via Const Reference (Lvalue Copy)
   */
  [[nodiscard]] bool tryPush(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
      return emplaceImpl(item);
  }

  /**
   * @brief Zero-Copy Single Message Enqueue via Rvalue Move Semantics
   */
  [[nodiscard]] bool tryPush(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
      return emplaceImpl(std::move(item));
  }

  /**
   * @brief High-Throughput Aggressive Spinning Push Loop
   */
  template <typename U>
  bool push(U&& item, uint32_t timeoutMs = DefaultPushTimeoutMs) {
    const auto startTime = std::chrono::steady_clock::now();
    const auto timeoutDuration = std::chrono::milliseconds(timeoutMs);
    uint64_t spinCounter = 0;

    while (true) {
      if (emplaceImpl(std::forward<U>(item))) return true;

      // Amortize time telemetry checks to minimize system call overhead inside hot spin paths
      if ((spinCounter++ & SpinCheckMask) == 0) [[unlikely]] {
        if (std::chrono::steady_clock::now() - startTime >= timeoutDuration) {
          _dropped.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
      }

      cpuPause(); 
    }
  }

  /**
   * @brief Low-Overhead Single Message Dequeue (Fast-Path Consumer)
   */
  [[nodiscard]] bool tryPop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
    const size_t tail = _tail.load(std::memory_order_relaxed);

    // Look-ahead cache matching: Queries local register state before accessing shared system bus
    if (tail == _cachedHead) [[unlikely]] {
      _cachedHead = _head.load(std::memory_order_acquire); // Acquire fence locks downstream data visibility
      if (tail == _cachedHead) [[unlikely]] {
        return false; 
      }
    }

    #if defined(__GNUC__) || defined(__clang__)
      // Stream next sequential memory address block straight into CPU L1 cache line ahead of time
      __builtin_prefetch(getPtr(increment(tail)), 0, 1);
    #endif

    T* objectPtr = getPtr(tail);
    item = std::move(*objectPtr);
    std::destroy_at(objectPtr); // Explicitly invoke destructor to finalize placement cell lifecycle

    // Release fence ensures slot data reads are finished before allowing the producer to reclaim space
    _tail.store(increment(tail), std::memory_order_release);
    return true;
  }

  /**
   * @brief High-Speed Fused Batch Processing Interface
   * Processes sequential blocks of messages while executing exactly ONE release fence per burst.
   */
  template <typename Callback>
  size_t popBatch(Callback&& callback, size_t maxBatchSize) noexcept {
    const size_t tail = _tail.load(std::memory_order_relaxed);
    
    if (tail == _cachedHead) {
      _cachedHead = _head.load(std::memory_order_acquire); 
      if (tail == _cachedHead) return 0; 
    }

    size_t localTail = tail;
    size_t processed = 0;

    while (localTail != _cachedHead && processed < maxBatchSize) [[likely]] {
      #if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(getPtr(increment(localTail)), 0, 1);
      #endif

      T* objectPtr = getPtr(localTail);
      callback(*objectPtr);
      std::destroy_at(objectPtr); 

      localTail = increment(localTail);
      ++processed;
    }

    // Amortize core interconnect invalidation overhead by committing all data movements in a single instruction
    _tail.store(localTail, std::memory_order_release);
    return processed;
  }

  /**
   * @brief In-Place Consumer Element Inspection Accessor
   * 
   * C++23 only: Uses explicit object parameter 'deducing this' to squash const/non-const duplicates into
   * a single template instance, preserving L1 instruction cache localization and optimization bounds.
   */
  template <typename Self>
  [[nodiscard]] auto* peek(this Self&& self) noexcept {
      const size_t tail = self._tail.load(std::memory_order_relaxed);
      
      if (tail == self._cachedHead) [[unlikely]] {
          self._cachedHead = self._head.load(std::memory_order_acquire);
          if (tail == self._cachedHead) [[unlikely]] {
              // Return nullptr on empty state to prevent UB branch trimming execution bugs.
              return static_cast<std::conditional_t<std::is_const_v<std::remove_reference_t<Self>>, const T*, T*>>(nullptr);
          }
      }
      return self.getPtr(tail);
  }

  [[nodiscard]] size_t getDropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }
  [[nodiscard]] size_t getPeakCount() const noexcept { return _peakOccupancy.load(std::memory_order_relaxed); }

private:
  static constexpr size_t CacheLine = getCacheLineSize();
  static constexpr size_t IndexMask = Capacity - 1;

  struct alignas(alignof(T)) StorageSlot {
      std::byte data[sizeof(T)];
  };

  // 1. Storage Array Buffer Frame Matrix.
  alignas(CacheLine) std::array<StorageSlot, Capacity> _buffer;

  // 2. Producer Write-State Cache Boundary (Isolates Producer thread modifications).
  alignas(CacheLine) std::atomic<size_t> _head;
  size_t _cachedTail{0}; // Thread-Local State: Read/Written exclusively by the Producer thread

  // 3. Consumer Read-State Cache Boundary (Isolates Consumer thread modifications).
  alignas(CacheLine) std::atomic<size_t> _tail;
  size_t _cachedHead{0}; // Thread-Local State: Read/Written exclusively by the Consumer thread

  // 4. Operational Diagnostics Metrics Boundary Zone
  alignas(CacheLine) std::atomic<size_t> _dropped;
  alignas(CacheLine) std::atomic<size_t> _peakOccupancy;

  /**
   * @brief Unified underlying constructor placement dispatcher
   */
  template <typename... Args>
  inline bool emplaceImpl(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
    const size_t head = _head.load(std::memory_order_relaxed);
    const size_t nextHead = increment(head);

    // High-speed branchless depth check targeting monotonic sequence counter layouts
    if ((nextHead - _cachedTail) > Capacity) [[unlikely]] {
      _cachedTail = _tail.load(std::memory_order_acquire);
      if ((nextHead - _cachedTail) > Capacity) [[unlikely]] {
        _dropped.fetch_add(1, std::memory_order_relaxed);
        return false; 
      }
    }

    // Modern placement constructor instantiation straight onto uninitialized memory byte slots
    std::construct_at(getPtr(head), std::forward<Args>(args)...);
    
    // Release fence guarantees object instantiation is fully flashed to the bus BEFORE advancing index
    _head.store(nextHead, std::memory_order_release);
    
    updatePeakOccupancy(nextHead);
    return true;
  }

  inline T* getPtr(size_t index) noexcept { 
      return reinterpret_cast<T*>(_buffer[index & IndexMask].data); 
  }
  
  inline const T* getPtr(size_t index) const noexcept { 
      return reinterpret_cast<const T*>(_buffer[index & IndexMask].data); 
  }
  
  static constexpr size_t increment(size_t index) noexcept { 
      return index + 1; 
  }

  void updatePeakOccupancy(size_t head) noexcept {
    const size_t tail = _tail.load(std::memory_order_relaxed);
    const size_t used = (head - tail);
    size_t currentPeak = _peakOccupancy.load(std::memory_order_relaxed);

    while (used > currentPeak) {
      if (_peakOccupancy.compare_exchange_weak(currentPeak, used, 
                                              std::memory_order_relaxed, 
                                              std::memory_order_relaxed)) {
          break;
      }
    }
  }
};

} // namespace spsc_ring_buffer