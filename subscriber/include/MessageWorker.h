/**
 * MessageWorker.h
 *
 * Worker thread for processing messages from the message queue
 *
 * Responsibilities:
 * - Dequeue messages from MessageQueue
 * - Validate message integrity
 * - Duplicate detection (sequence-based)
 * - Business logic processing
 * - Send statistics to monitoring
 * - Return buffers to pool
 *
 * Design:
 * - Single consumer from message queue (SPSC)
 * - Hash-based duplicate detection
 * - Extensible message type handlers
 * - Graceful shutdown support
 */

#ifndef AERON_EXAMPLE_MESSAGE_WORKER_H
#define AERON_EXAMPLE_MESSAGE_WORKER_H

#include "MessageBuffer.h"
#include "BufferPool.h"
#include "MessageQueue.h"
#include "SPSCQueue.h"
#include <atomic>
#include <unordered_set>
#include <thread>
#include <functional>

namespace aeron {
namespace example {

/**
 * Message Worker
 *
 * Processes messages from the queue in a separate thread
 */
class MessageWorker {
public:
    /**
     * Business logic callback type
     *
     * Called for each validated, non-duplicate message
     * Parameters: MessageBuffer pointer
     */
    using MessageHandler = std::function<void(const MessageBuffer*)>;

    /**
     * Constructor
     *
     * @param queue Message queue (source of messages)
     * @param pool Buffer pool (for returning buffers)
     * @param stats_queue Statistics queue (for monitoring)
     */
    MessageWorker(
        MessageBufferQueue& queue,
        MessageBufferPool& pool,
        MessageStatsQueue& stats_queue);

    ~MessageWorker();

    // Non-copyable
    MessageWorker(const MessageWorker&) = delete;
    MessageWorker& operator=(const MessageWorker&) = delete;

    /**
     * Set business logic handler
     *
     * This handler is called for each validated message
     * after duplicate detection.
     */
    void setMessageHandler(MessageHandler handler);

    /**
     * Start worker thread
     */
    void start();

    /**
     * Stop worker thread (graceful shutdown)
     */
    void stop();

    /**
     * Check if worker is running
     */
    bool isRunning() const;

    /**
     * Worker statistics
     */
    struct Statistics {
        uint64_t messages_processed;      // Successfully processed
        uint64_t messages_invalid;        // Failed validation
        uint64_t messages_duplicate;      // Duplicate detected
        uint64_t queue_empty_count;       // Queue was empty
        double avg_processing_time_us;    // Average processing time
        double avg_queue_depth;           // Average queue depth
    };

    /**
     * Get worker statistics
     */
    Statistics getStatistics() const;

    /**
     * Print worker statistics
     */
    void printStatistics() const;

private:
    // Worker thread main loop
    void workerThreadMain();

    // Message processing steps
    bool validateMessage(const MessageBuffer* buf);
    bool checkDuplicate(const MessageBuffer* buf);
    void processMessage(const MessageBuffer* buf);
    void sendToMonitoring(const MessageBuffer* buf);

    // Message type handlers (extensible)
    void handleOrderNew(const MessageBuffer* buf);
    void handleOrderExecution(const MessageBuffer* buf);
    void handleOrderModify(const MessageBuffer* buf);
    void handleOrderCancel(const MessageBuffer* buf);
    void handleQuoteUpdate(const MessageBuffer* buf);

    // References (not owned)
    MessageBufferQueue& message_queue_;
    MessageBufferPool& buffer_pool_;
    MessageStatsQueue& stats_queue_;

    // Worker thread
    std::unique_ptr<std::thread> worker_thread_;
    std::atomic<bool> running_;

    // Business logic handler (optional)
    MessageHandler message_handler_;

    // Duplicate detection
    std::unordered_set<uint64_t> seen_sequences_;

    // Statistics
    std::atomic<uint64_t> messages_processed_;
    std::atomic<uint64_t> messages_invalid_;
    std::atomic<uint64_t> messages_duplicate_;
    std::atomic<uint64_t> queue_empty_count_;

    // Performance metrics
    uint64_t total_processing_time_ns_;
    uint64_t processing_count_;
    uint64_t total_queue_depth_;
    uint64_t queue_depth_samples_;
};

} // namespace example
} // namespace aeron

#endif // AERON_EXAMPLE_MESSAGE_WORKER_H
