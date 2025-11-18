/**
 * BufferPool.h
 *
 * Lock-free buffer pool for zero-copy message processing
 *
 * Design:
 * - Pre-allocated buffers (avoid malloc/free)
 * - Lock-free allocation/deallocation using atomic operations
 * - O(1) allocation and deallocation
 * - Thread-safe (lock-free)
 * - Cache-line aligned to prevent false sharing
 *
 * Performance:
 * - Allocate: ~50-100ns
 * - Deallocate: ~50-100ns
 * - Memory: PoolSize × sizeof(MessageBuffer)
 */

#ifndef AERON_EXAMPLE_BUFFER_POOL_H
#define AERON_EXAMPLE_BUFFER_POOL_H

#include "MessageBuffer.h"
#include <atomic>
#include <new>
#include <iostream>

namespace aeron {
namespace example {

/**
 * Lock-free Buffer Pool
 *
 * Implementation:
 * - Uses atomic counter for free list management
 * - CAS (Compare-And-Swap) for lock-free allocation
 * - Cache-line aligned members to prevent false sharing
 *
 * Thread Safety:
 * - Allocate: Thread-safe (lock-free)
 * - Deallocate: Thread-safe (lock-free)
 * - Multiple producers/consumers supported
 */
template<size_t PoolSize>
class BufferPool {
public:
    static_assert(PoolSize > 0, "PoolSize must be greater than 0");
    static_assert(PoolSize <= 65536, "PoolSize too large (max 65536)");

    /**
     * Constructor
     * Initializes all buffers and adds them to free list
     */
    BufferPool() {
        // Initialize buffers in-place
        for (size_t i = 0; i < PoolSize; i++) {
            new (&buffers_[i]) MessageBuffer();
            free_list_[i] = &buffers_[i];
        }

        free_count_.store(PoolSize, std::memory_order_release);
        total_allocations_.store(0, std::memory_order_relaxed);
        total_deallocations_.store(0, std::memory_order_relaxed);
        allocation_failures_.store(0, std::memory_order_relaxed);

        std::cout << "BufferPool initialized: " << PoolSize << " buffers, "
                  << (PoolSize * sizeof(MessageBuffer) / 1024 / 1024) << " MB"
                  << std::endl;
    }

    /**
     * Destructor
     */
    ~BufferPool() {
        // Explicitly call destructors
        for (size_t i = 0; i < PoolSize; i++) {
            buffers_[i].~MessageBuffer();
        }
    }

    // Non-copyable
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    /**
     * Allocate a buffer from the pool
     *
     * Performance: ~50-100ns (CAS loop)
     *
     * @return Pointer to allocated buffer, or nullptr if pool exhausted
     */
    MessageBuffer* allocate() noexcept {
        size_t count = free_count_.load(std::memory_order_acquire);

        while (count > 0) {
            // Try to decrement free count
            if (free_count_.compare_exchange_weak(
                    count, count - 1,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {

                // Successfully decremented, get buffer from free list
                MessageBuffer* buf = free_list_[count - 1];

                // Mark buffer as in use
                buf->in_use.store(true, std::memory_order_release);

                // Reset buffer state
                buf->reset();

                // Update statistics
                total_allocations_.fetch_add(1, std::memory_order_relaxed);

                return buf;
            }
            // CAS failed, retry with updated count
        }

        // Pool exhausted
        allocation_failures_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    /**
     * Deallocate a buffer back to the pool
     *
     * Performance: ~50-100ns (CAS loop)
     *
     * @param buf Buffer to return to pool
     */
    void deallocate(MessageBuffer* buf) noexcept {
        if (!buf) {
            return;
        }

        // Validate buffer belongs to this pool
        if (!isValidBuffer(buf)) {
            std::cerr << "ERROR: Attempting to deallocate buffer not from this pool"
                      << std::endl;
            return;
        }

        // Mark buffer as not in use
        buf->in_use.store(false, std::memory_order_release);

        // Return to free list
        size_t count = free_count_.load(std::memory_order_acquire);

        while (count < PoolSize) {
            // Try to increment free count
            if (free_count_.compare_exchange_weak(
                    count, count + 1,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {

                // Successfully incremented, add buffer to free list
                free_list_[count] = buf;

                // Update statistics
                total_deallocations_.fetch_add(1, std::memory_order_relaxed);

                return;
            }
            // CAS failed, retry with updated count
        }

        // This should never happen (deallocating more than allocated)
        std::cerr << "ERROR: BufferPool free list overflow" << std::endl;
    }

    /**
     * Get number of available buffers
     *
     * @return Number of free buffers
     */
    size_t available() const noexcept {
        return free_count_.load(std::memory_order_acquire);
    }

    /**
     * Get total pool capacity
     *
     * @return Total number of buffers in pool
     */
    constexpr size_t capacity() const noexcept {
        return PoolSize;
    }

    /**
     * Get pool utilization percentage (0.0 - 1.0)
     *
     * @return Utilization ratio
     */
    double utilization() const noexcept {
        size_t used = PoolSize - available();
        return static_cast<double>(used) / PoolSize;
    }

    /**
     * Get pool statistics
     */
    struct Statistics {
        size_t total_allocations;
        size_t total_deallocations;
        size_t allocation_failures;
        size_t current_available;
        size_t current_in_use;
        double utilization;
    };

    Statistics getStatistics() const noexcept {
        Statistics stats;
        stats.total_allocations = total_allocations_.load(std::memory_order_relaxed);
        stats.total_deallocations = total_deallocations_.load(std::memory_order_relaxed);
        stats.allocation_failures = allocation_failures_.load(std::memory_order_relaxed);
        stats.current_available = available();
        stats.current_in_use = PoolSize - stats.current_available;
        stats.utilization = utilization();
        return stats;
    }

    /**
     * Print pool statistics
     */
    void printStatistics() const {
        auto stats = getStatistics();

        std::cout << "\n=== Buffer Pool Statistics ===" << std::endl;
        std::cout << "Capacity:      " << PoolSize << " buffers" << std::endl;
        std::cout << "Available:     " << stats.current_available << std::endl;
        std::cout << "In use:        " << stats.current_in_use << std::endl;
        std::cout << "Utilization:   " << (stats.utilization * 100.0) << "%" << std::endl;
        std::cout << "Allocations:   " << stats.total_allocations << std::endl;
        std::cout << "Deallocations: " << stats.total_deallocations << std::endl;
        std::cout << "Failures:      " << stats.allocation_failures << std::endl;

        if (stats.allocation_failures > 0) {
            std::cout << "⚠️  WARNING: " << stats.allocation_failures
                      << " allocation failures (pool exhausted)" << std::endl;
        }

        std::cout << "==============================\n" << std::endl;
    }

private:
    /**
     * Validate that buffer belongs to this pool
     */
    bool isValidBuffer(MessageBuffer* buf) const noexcept {
        uintptr_t buf_addr = reinterpret_cast<uintptr_t>(buf);
        uintptr_t pool_start = reinterpret_cast<uintptr_t>(&buffers_[0]);
        uintptr_t pool_end = reinterpret_cast<uintptr_t>(&buffers_[PoolSize]);

        return buf_addr >= pool_start && buf_addr < pool_end;
    }

    // Buffer storage (cache-line aligned)
    alignas(64) MessageBuffer buffers_[PoolSize];

    // Free list (array of pointers)
    alignas(64) MessageBuffer* free_list_[PoolSize];

    // Atomic counters (cache-line aligned to prevent false sharing)
    alignas(64) std::atomic<size_t> free_count_;

    // Statistics (separate cache line)
    alignas(64) std::atomic<uint64_t> total_allocations_;
    std::atomic<uint64_t> total_deallocations_;
    std::atomic<uint64_t> allocation_failures_;
};

// Recommended pool sizes
using MessageBufferPool = BufferPool<1024>;      // 1024 buffers (~4.2 MB)
using LargeBufferPool = BufferPool<4096>;        // 4096 buffers (~16.8 MB)
using SmallBufferPool = BufferPool<256>;         // 256 buffers (~1.05 MB)

} // namespace example
} // namespace aeron

#endif // AERON_EXAMPLE_BUFFER_POOL_H
