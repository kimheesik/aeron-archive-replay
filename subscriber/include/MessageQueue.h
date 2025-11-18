/**
 * MessageQueue.h
 *
 * Zero-copy message queue using pointer passing
 *
 * Design:
 * - Lock-free SPSC (Single Producer Single Consumer)
 * - Passes MessageBuffer pointers (NOT data)
 * - Ring buffer with power-of-2 size
 * - Cache-line aligned to prevent false sharing
 *
 * Performance:
 * - Enqueue: ~50ns (pointer copy only)
 * - Dequeue: ~50ns (pointer copy only)
 * - Zero data copy (only pointer transfer)
 * - Memory: Size × sizeof(MessageBuffer*) = Size × 8 bytes
 *
 * Usage:
 *   MessageQueue<4096> queue;  // 4K slots = 32KB memory
 *
 *   // Producer (Subscriber thread)
 *   MessageBuffer* buf = pool.allocate();
 *   // ... fill buffer ...
 *   queue.enqueue(buf);
 *
 *   // Consumer (Worker thread)
 *   MessageBuffer* buf;
 *   if (queue.dequeue(buf)) {
 *       // ... process buffer ...
 *       pool.deallocate(buf);
 *   }
 */

#ifndef AERON_EXAMPLE_MESSAGE_QUEUE_H
#define AERON_EXAMPLE_MESSAGE_QUEUE_H

#include "MessageBuffer.h"
#include <atomic>
#include <iostream>

namespace aeron {
namespace example {

/**
 * Zero-Copy Message Queue
 *
 * Implementation:
 * - Ring buffer with head/tail pointers
 * - Power-of-2 size for fast modulo (bitwise AND)
 * - Lock-free using atomic operations
 * - Cache-line alignment to prevent false sharing
 *
 * Thread Safety:
 * - Single Producer Single Consumer (SPSC)
 * - One thread enqueues, one thread dequeues
 * - Lock-free and wait-free
 */
template<size_t Size>
class MessageQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static_assert(Size >= 16, "Size must be at least 16");
    static_assert(Size <= 65536, "Size must be at most 65536");

public:
    /**
     * Constructor
     */
    MessageQueue() : head_(0), tail_(0) {
        // Initialize all slots to nullptr
        for (size_t i = 0; i < Size; i++) {
            buffer_[i] = nullptr;
        }

        total_enqueued_.store(0, std::memory_order_relaxed);
        total_dequeued_.store(0, std::memory_order_relaxed);
        enqueue_failures_.store(0, std::memory_order_relaxed);

        std::cout << "MessageQueue initialized: " << Size << " slots, "
                  << (Size * sizeof(MessageBuffer*) / 1024) << " KB"
                  << std::endl;
    }

    /**
     * Destructor
     */
    ~MessageQueue() {
        // Warn if queue is not empty
        size_t remaining = size();
        if (remaining > 0) {
            std::cerr << "WARNING: MessageQueue destroyed with "
                      << remaining << " messages still in queue" << std::endl;
        }
    }

    // Non-copyable
    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    /**
     * Enqueue a message buffer pointer
     *
     * Performance: ~50ns (pointer copy + atomic store)
     *
     * @param buf Message buffer pointer
     * @return true if enqueued, false if queue is full
     */
    bool enqueue(MessageBuffer* buf) noexcept {
        if (!buf) {
            return false;
        }

        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Size - 1);

        // Check if queue is full
        if (next_tail == head_.load(std::memory_order_acquire)) {
            enqueue_failures_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Store buffer pointer
        buffer_[current_tail] = buf;

        // Update tail (release semantics to ensure visibility)
        tail_.store(next_tail, std::memory_order_release);

        // Update statistics
        total_enqueued_.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    /**
     * Dequeue a message buffer pointer
     *
     * Performance: ~50ns (atomic load + pointer copy)
     *
     * @param buf Output parameter for buffer pointer
     * @return true if dequeued, false if queue is empty
     */
    bool dequeue(MessageBuffer*& buf) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Check if queue is empty
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }

        // Get buffer pointer
        buf = buffer_[current_head];

        // Clear slot (optional, for debugging)
        buffer_[current_head] = nullptr;

        // Update head (release semantics)
        head_.store((current_head + 1) & (Size - 1), std::memory_order_release);

        // Update statistics
        total_dequeued_.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    /**
     * Get current queue size
     *
     * Note: This is an approximation in multi-threaded context
     *
     * @return Number of messages in queue
     */
    size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h) & (Size - 1);
    }

    /**
     * Check if queue is empty
     *
     * @return true if empty
     */
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /**
     * Check if queue is full
     *
     * @return true if full
     */
    bool full() const noexcept {
        const size_t current_tail = tail_.load(std::memory_order_acquire);
        const size_t next_tail = (current_tail + 1) & (Size - 1);
        return next_tail == head_.load(std::memory_order_acquire);
    }

    /**
     * Get queue capacity
     *
     * @return Maximum number of messages (Size - 1)
     */
    constexpr size_t capacity() const noexcept {
        return Size - 1;  // One slot is always unused
    }

    /**
     * Get queue utilization percentage (0.0 - 1.0)
     *
     * @return Utilization ratio
     */
    double utilization() const noexcept {
        return static_cast<double>(size()) / capacity();
    }

    /**
     * Queue statistics
     */
    struct Statistics {
        size_t total_enqueued;
        size_t total_dequeued;
        size_t enqueue_failures;
        size_t current_size;
        size_t capacity;
        double utilization;
    };

    /**
     * Get queue statistics
     */
    Statistics getStatistics() const noexcept {
        Statistics stats;
        stats.total_enqueued = total_enqueued_.load(std::memory_order_relaxed);
        stats.total_dequeued = total_dequeued_.load(std::memory_order_relaxed);
        stats.enqueue_failures = enqueue_failures_.load(std::memory_order_relaxed);
        stats.current_size = size();
        stats.capacity = capacity();
        stats.utilization = utilization();
        return stats;
    }

    /**
     * Print queue statistics
     */
    void printStatistics() const {
        auto stats = getStatistics();

        std::cout << "\n=== Message Queue Statistics ===" << std::endl;
        std::cout << "Capacity:      " << stats.capacity << " messages" << std::endl;
        std::cout << "Current size:  " << stats.current_size << std::endl;
        std::cout << "Utilization:   " << (stats.utilization * 100.0) << "%" << std::endl;
        std::cout << "Enqueued:      " << stats.total_enqueued << std::endl;
        std::cout << "Dequeued:      " << stats.total_dequeued << std::endl;
        std::cout << "Failures:      " << stats.enqueue_failures << std::endl;

        if (stats.enqueue_failures > 0) {
            std::cout << "⚠️  WARNING: " << stats.enqueue_failures
                      << " enqueue failures (queue full)" << std::endl;
        }

        std::cout << "================================\n" << std::endl;
    }

    /**
     * Clear all messages in queue
     *
     * WARNING: This does NOT deallocate buffers!
     * Caller must ensure buffers are returned to pool before clearing.
     */
    void clear() noexcept {
        head_.store(0, std::memory_order_release);
        tail_.store(0, std::memory_order_release);
    }

private:
    // Ring buffer (cache-line aligned)
    alignas(64) MessageBuffer* buffer_[Size];

    // Head and tail pointers (separate cache lines to prevent false sharing)
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;

    // Statistics (separate cache line)
    alignas(64) std::atomic<uint64_t> total_enqueued_;
    std::atomic<uint64_t> total_dequeued_;
    std::atomic<uint64_t> enqueue_failures_;
};

// Recommended queue sizes
using MessageBufferQueue = MessageQueue<4096>;   // 4K slots (~32 KB)
using LargeMessageQueue = MessageQueue<16384>;   // 16K slots (~128 KB)
using SmallMessageQueue = MessageQueue<1024>;    // 1K slots (~8 KB)

} // namespace example
} // namespace aeron

#endif // AERON_EXAMPLE_MESSAGE_QUEUE_H
