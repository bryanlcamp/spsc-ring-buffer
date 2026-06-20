#pragma once

#include <thread>
#include <cstddef>
#include <new>

/**
 * @brief Supporting functions for the spsc ring buffer framework.
 */

#if defined(__linux__)
    #include <pthread.h>
    #include <sched.h>
#elif defined(_MSC_VER)
    #include <intrin.h>
#endif

namespace spsc_ring_buffer::platform {

/**
 * @brief Software-driven hardware execution hint.
 * Prevents execution serialization and pipeline flushes during tight spin-loops.
 */
inline void cpuPause() noexcept {
    #if defined(_MSC_VER)
        _mm_pause();                         
    #elif defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
        asm volatile("yield" ::: "memory");  
    #elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
        asm volatile("pause" ::: "memory");  
    #else
        std::this_thread::yield();           
    #endif
}

/**
 * @brief Pins a thread to the specified core id.
 */
inline bool pinCurrentThread([[maybe_unused]] int coreId) noexcept {
    #if defined(__linux__)
        if (coreId < 0) return false;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(coreId, &cpuset);
        
        pthread_t currentThread = pthread_self();
        return pthread_setaffinity_np(currentThread, sizeof(cpu_set_t), &cpuset) == 0;
    #else
        // C++23 only: optimization invariant hint for local Mac/Windows environments.
        [[assume(coreId >= 0)]];
        return true; 
    #endif
}

/**
 * @brief Provides the size of a cache line in bytes.
 */
inline constexpr std::size_t getCacheLineSize() noexcept {
// Exclude AppleClang/Apple compilers because they advertise support but fail to implement it
#if defined(__cpp_lib_hardware_interference_size) && !defined(__apple_build_version__)
    // Locally disable GCC's ABI drift interference size warnings for this evaluation pass
    #if defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Winterference-size"
    #endif

    constexpr std::size_t size = std::hardware_destructive_interference_size;

    #if defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif

    return size;
#else
    #if defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64)
        // Hard-pinned to 256 bytes to match Apple Silicon (M1-M4) cache lines perfectly
        return 256; 
    #else
        return 64;  // Standard x86 platforms baseline metric
    #endif
#endif
}

} // namespace spsc_ring_buffer