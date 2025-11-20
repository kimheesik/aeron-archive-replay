#pragma once

#include <atomic>
#include <thread>
#include <string>
#include <chrono>
#include <fstream>
#include <iostream>

namespace aeron {
namespace example {

/**
 * CheckpointManager - Async checkpoint persistence with minimal overhead
 *
 * Architecture:
 *   Main Thread (Fast Path):
 *     - update() -> Atomic store (~10 ns)
 *     - No I/O, no locks
 *
 *   Background Thread (Slow Path):
 *     - Periodic flush to disk (every N seconds)
 *     - Atomic rename for crash safety
 *
 * Performance:
 *   - Main thread overhead: ~10 ns (negligible)
 *   - Background flush: 10-50 ms (doesn't block main thread)
 *   - Data loss risk: Maximum N seconds (configurable)
 *
 * Inspired by Redis AOF (Append-Only File) architecture
 */
class CheckpointManager {
public:
    /**
     * Checkpoint data stored in memory with atomic access
     */
    struct CheckpointData {
        std::atomic<int64_t> last_sequence_number{0};
        std::atomic<int64_t> last_position{0};
        std::atomic<int64_t> message_count{0};
        std::atomic<int64_t> timestamp_ns{0};
    };

private:
    CheckpointData data_;                  // In-memory checkpoint (atomic)
    std::string checkpoint_file_;          // Path to checkpoint file
    std::atomic<bool> running_{true};      // Background thread control
    std::thread flush_thread_;             // Background flush thread
    std::chrono::seconds flush_interval_;  // Flush interval (default: 1 sec)

    // Statistics
    std::atomic<uint64_t> flush_count_{0};
    std::atomic<uint64_t> flush_failures_{0};

public:
    /**
     * Constructor
     *
     * @param file Checkpoint file path
     * @param flush_interval_sec Flush interval in seconds (default: 1)
     */
    explicit CheckpointManager(const std::string& file, int flush_interval_sec = 1);

    /**
     * Destructor
     * - Stops background thread
     * - Performs final flush
     */
    ~CheckpointManager();

    // Non-copyable, non-movable
    CheckpointManager(const CheckpointManager&) = delete;
    CheckpointManager& operator=(const CheckpointManager&) = delete;
    CheckpointManager(CheckpointManager&&) = delete;
    CheckpointManager& operator=(CheckpointManager&&) = delete;

    /**
     * Update checkpoint (FAST PATH - called from main thread)
     *
     * Performance: ~10 ns (atomic stores only, no I/O)
     *
     * @param sequence Last received sequence number
     * @param position Last received Aeron position
     * @param msg_count Total message count
     */
    void update(int64_t sequence, int64_t position, int64_t msg_count);

    /**
     * Force immediate flush to disk
     * Blocks until flush completes (~10-50 ms)
     */
    void forceFlush();

    /**
     * Get last sequence number
     */
    int64_t getLastSequence() const;

    /**
     * Get last position
     */
    int64_t getLastPosition() const;

    /**
     * Get message count
     */
    int64_t getMessageCount() const;

    /**
     * Get timestamp
     */
    int64_t getTimestamp() const;

    /**
     * Get flush statistics
     */
    void printStatistics() const;

private:
    /**
     * Background flush loop
     * Runs in separate thread
     */
    void flushLoop();

    /**
     * Flush checkpoint to disk (SLOW PATH - background thread)
     *
     * Implementation:
     *   1. Read atomic values (consistent snapshot)
     *   2. Write to temporary file
     *   3. Atomic rename (POSIX guarantees atomicity)
     *
     * Performance: 10-50 ms (SSD)
     */
    void flush();

    /**
     * Load checkpoint from disk
     * Called during initialization
     */
    void load();

    /**
     * Get current time in nanoseconds
     */
    static int64_t getCurrentTimeNanos();
};

} // namespace example
} // namespace aeron
