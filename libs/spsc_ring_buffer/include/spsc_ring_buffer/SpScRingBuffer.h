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

inline constexpr size_t   DefaultCapacity = 1024;
inline constexpr uint64_t SpinCheckInterval = 1024;
inline constexpr uint64_t SpinCheckMask = SpinCheckInterval - 1;
inline constexpr uint32_t DefaultPushTimeoutMs = 5000;

/**
 * @brief Lock free, zero allocation, single producer single consumer ring buffer.
 *
 * @tparam T. Payload. Does not require constructability.        
 * @tparam Capacity. Ring buffer capacity. Must be a power of 2 due to bitwise operation.
 */
template <typename T, size_t Capacity = DefaultCapacity>
class SpScRingBuffer {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of 2");

public:
  SpScRingBuffer() : 
    _head(0),            // the head index (this ring buffer is circular)
    _tail(0),            // the tail index (this ring buffer is circular)
    _dropped(0),         // the total amoun of packets dropped
    _peakOccupancy(0) {} // the maximum size of the ring buffer

  /**
   * @brief Destructor tries to empty the queue before deletion.
   */
  ~SpScRingBuffer() noexcept {
      T dummy;
      while (tryPop(dummy)); 
  }

  // Both copying and moving the ring buffer are disallowed.
  SpScRingBuffer(const SpScRingBuffer&) = delete;
  SpScRingBuffer& operator=(const SpScRingBuffer&) = delete;
  SpScRingBuffer(SpScRingBuffer&&) = delete;
  SpScRingBuffer& operator=(SpScRingBuffer&&) = delete;

  /**
   * @brief Enqueue via Lvalue copy.
   * @returns False if: 
   *            (1) The value is not allowed to be copied.
   *            (2) The ring buffer is full.
   *          True: otherwise.
   */
  [[nodiscard]] bool tryPush(const T& item) noexcept(
    std::is_nothrow_copy_constructible_v<T>) {
      return emplaceImpl(item);
  }

  /**
   * @brief Enqueue via Rvalue move.
   * @returns False if: 
   *            (1) The value is not allowed to be copied.
   *            (2) The ring buffer is full.
   *          True: otherwise.
   */
  [[nodiscard]] bool tryPush(T&& item) noexcept(
    std::is_nothrow_move_constructible_v<T>) {
      return emplaceImpl(std::move(item));
  }

  /**
   * @brief Blocks until the payload is successfully pushed into the ring buffer.
   *        Gives up trying if the default timeout is reached.
   *        See DefaultPushTimeoutMs above. Modify this variable how you wish.
   * @returns True:  The payload was successfully inserted into the ring buffer.
   *          False: Otherwise.
   */
  template <typename U>
  bool push(U&& item, uint32_t timeoutMs = DefaultPushTimeoutMs) {
    const auto startTime = std::chrono::steady_clock::now();
    const auto timeoutDuration = std::chrono::milliseconds(timeoutMs);
    uint64_t spinCounter = 0;

    while (true) {
      if (emplaceImpl(std::forward<U>(item))) {
        // The item was successfully addded to the ring buffer.
        return true;
      }

      // The item could not be added to the buffer. Keep trying.
      // If we have exceeded the timeout duration then stop and return false.
      // It's likely we will have popped some items off the queue to make some space.
      if ((spinCounter++ & SpinCheckMask) == 0) [[unlikely]] {
        if (std::chrono::steady_clock::now() - startTime >= timeoutDuration) {
          _dropped.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
      }

      // To prevent a completely tight loop, allow a context switch.
      spsc_ring_buffer::platform::cpuPause(); 
    }
  }

  /**
   * @brief Returns the item in the queue that has been waiting the longest.
   * @returns Immediately returns false if the queue is empty.
   */
  [[nodiscard]] bool tryPop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
    const size_t tail = _tail.load(std::memory_order_relaxed);

    // Look-ahead cache matching.
    if (tail == _cachedHead) [[unlikely]] {
      // Acquire memory fence that locks downstream data visibility.
      _cachedHead = _head.load(std::memory_order_acquire); 
      if (tail == _cachedHead) [[unlikely]] {
        return false; 
      }
    }

    // There's an item in the queue to return it.
    // Get the item to return and reclaim its space.

    #if defined(__GNUC__) || defined(__clang__)
      // Update the tail pointer.
      __builtin_prefetch(getPtr(increment(tail)), 0, 1);
    #endif

    // Move the item to return out of the queue.
    T* objectPtr = getPtr(tail);
    item = std::move(*objectPtr);
    std::destroy_at(objectPtr);

    // Release the memory fence and reclaim space.
    _tail.store(increment(tail), std::memory_order_release);
    return true;
  }

  /**
   * @brief Drains the queue.
   * Processes sequential blocks of messages while executing exactly ONE release fence per burst.
   */
  template <typename Callback>
  size_t popBatch(Callback&& callback, size_t maxBatchSize) noexcept {
    const size_t tail = _tail.load(std::memory_order_relaxed);
    
    // Check if the ring buffer is empty.
    // If not, set a memory fence for downstream iterations.
    if (tail == _cachedHead) {
      _cachedHead = _head.load(std::memory_order_acquire);
      if (tail == _cachedHead) return 0; 
    }

    size_t localTail = tail;
    size_t processed = 0;

    // Iterate through queue and keep returning items one-by-one.
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

    // Release the memory fence.
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


  [[nodiscard]] constexpr size_t getCapacity() const noexcept { return Capacity; }
  [[nodiscard]] size_t getDropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }
  [[nodiscard]] size_t getPeakCount() const noexcept { return _peakOccupancy.load(std::memory_order_relaxed); }

private:
  static constexpr size_t CacheLine = spsc_ring_buffer::platform::getCacheLineSize();
  static constexpr size_t IndexMask = Capacity - 1;

  struct alignas(alignof(T)) StorageSlot {
      std::byte data[sizeof(T)];
  };

  // The array holding the buffer contents.
  alignas(CacheLine) std::array<StorageSlot, Capacity> _buffer;

  // Producer write cache boundary. Read/written to exclusively by the producer thread.
  alignas(CacheLine) std::atomic<size_t> _head;
  size_t _cachedTail{0};

  // Consumer read cache boundary. Read/written to exclusively by consumer thread.
  alignas(CacheLine) std::atomic<size_t> _tail;
  size_t _cachedHead{0};

  // Simple diagnostics.
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