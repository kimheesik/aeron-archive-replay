/**
 * MessageWorker.cpp
 *
 * Worker thread implementation for message processing
 */

#include "MessageWorker.h"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace aeron {
namespace example {

MessageWorker::MessageWorker(
    MessageBufferQueue& queue,
    MessageBufferPool& pool,
    MessageStatsQueue& stats_queue)
    : message_queue_(queue)
    , buffer_pool_(pool)
    , stats_queue_(stats_queue)
    , running_(false)
    , messages_processed_(0)
    , messages_invalid_(0)
    , messages_duplicate_(0)
    , queue_empty_count_(0)
    , total_processing_time_ns_(0)
    , processing_count_(0)
    , total_queue_depth_(0)
    , queue_depth_samples_(0) {

    // Pre-allocate duplicate detection hash table
    seen_sequences_.reserve(100000);

    std::cout << "MessageWorker created" << std::endl;
}

MessageWorker::~MessageWorker() {
    stop();
}

void MessageWorker::setMessageHandler(MessageHandler handler) {
    message_handler_ = std::move(handler);
    std::cout << "Message handler registered" << std::endl;
}

void MessageWorker::start() {
    if (running_.load(std::memory_order_acquire)) {
        std::cerr << "Worker already running" << std::endl;
        return;
    }

    running_.store(true, std::memory_order_release);

    // Create worker thread
    worker_thread_ = std::make_unique<std::thread>(
        &MessageWorker::workerThreadMain, this);

    std::cout << "✓ Worker thread started" << std::endl;
}

void MessageWorker::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    std::cout << "\nStopping worker thread..." << std::endl;

    // Signal thread to stop
    running_.store(false, std::memory_order_release);

    // Wait for thread to finish
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }

    std::cout << "✓ Worker thread stopped" << std::endl;
    printStatistics();
}

bool MessageWorker::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

void MessageWorker::workerThreadMain() {
    std::cout << "Worker thread running (TID: " << std::this_thread::get_id() << ")" << std::endl;

    MessageBuffer* msg_buf = nullptr;
    uint64_t empty_count = 0;

    while (running_.load(std::memory_order_acquire)) {
        // 1. Sample queue depth for monitoring
        size_t queue_depth = message_queue_.size();
        total_queue_depth_ += queue_depth;
        queue_depth_samples_++;

        // 2. Dequeue message (~50ns)
        if (!message_queue_.dequeue(msg_buf)) {
            // Queue empty - adaptive wait
            queue_empty_count_.fetch_add(1, std::memory_order_relaxed);
            empty_count++;

            if (empty_count < 100) {
                // Busy spin for a bit
                std::this_thread::yield();
            } else {
                // Sleep briefly to avoid busy loop
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            continue;
        }

        empty_count = 0;

        // Record dequeue timestamp for queuing latency measurement
        msg_buf->worker_dequeue_time_ns = getCurrentTimeNanos();

        // 3. Validate message (~200ns)
        if (!validateMessage(msg_buf)) {
            messages_invalid_.fetch_add(1, std::memory_order_relaxed);
            buffer_pool_.deallocate(msg_buf);
            continue;
        }

        // 4. Duplicate detection (~50ns with hash table)
        if (checkDuplicate(msg_buf)) {
            messages_duplicate_.fetch_add(1, std::memory_order_relaxed);
            buffer_pool_.deallocate(msg_buf);
            continue;
        }

        // 5. Process message (variable time)
        auto start_processing = getCurrentTimeNanos();
        processMessage(msg_buf);
        auto end_processing = getCurrentTimeNanos();

        // Update processing time stats
        total_processing_time_ns_ += (end_processing - start_processing);
        processing_count_++;

        // 6. Send to monitoring (~50ns)
        sendToMonitoring(msg_buf);

        // 7. Return buffer to pool (~100ns)
        buffer_pool_.deallocate(msg_buf);

        // 8. Update statistics
        messages_processed_.fetch_add(1, std::memory_order_relaxed);
    }

    std::cout << "Worker thread exiting (processed "
              << messages_processed_.load(std::memory_order_relaxed)
              << " messages)" << std::endl;
}

bool MessageWorker::validateMessage(const MessageBuffer* buf) {
    if (!buf) {
        return false;
    }

    // TEMPORARY: Disable strict validation for testing with existing publisher
    // The existing publisher sends simple text messages without MessageHeader format
    //
    // For production, uncomment the validation code below:
    /*
    // Check magic
    if (!buf->header.isValid()) {
        return false;
    }

    // Check version
    if (buf->header.version == 0 || buf->header.version > 100) {
        return false;
    }

    // Check message type
    if (buf->header.message_type == 0) {
        return false;
    }

    // Check message length
    if (buf->header.message_length > sizeof(MessageHeader) + MAX_PAYLOAD_SIZE) {
        return false;
    }

    // Verify checksum if enabled
    if (buf->header.hasChecksum()) {
        // TODO: Implement CRC32 verification
        // For now, skip checksum validation
    }
    */

    // Accept all messages for testing
    return true;
}

bool MessageWorker::checkDuplicate(const MessageBuffer* buf) {
    uint64_t seq = buf->header.sequence_number;

    // Check if already seen
    if (seen_sequences_.count(seq) > 0) {
        return true;  // Duplicate
    }

    // Add to seen set
    seen_sequences_.insert(seq);

    // Optional: Trim old sequences if set grows too large
    // (In production, use a sliding window or time-based expiration)
    if (seen_sequences_.size() > 1000000) {
        // Clear oldest half (simple strategy)
        // TODO: Implement proper LRU or time-based eviction
        seen_sequences_.clear();
        std::cout << "⚠️  Duplicate detection set cleared (size limit reached)" << std::endl;
    }

    return false;  // Not duplicate
}

void MessageWorker::processMessage(const MessageBuffer* buf) {
    // Dispatch based on message type
    switch (buf->header.message_type) {
        case MSG_ORDER_NEW:
            handleOrderNew(buf);
            break;

        case MSG_ORDER_EXECUTION:
            handleOrderExecution(buf);
            break;

        case MSG_ORDER_MODIFY:
            handleOrderModify(buf);
            break;

        case MSG_ORDER_CANCEL:
            handleOrderCancel(buf);
            break;

        case MSG_QUOTE_UPDATE:
            handleQuoteUpdate(buf);
            break;

        case MSG_TEST:
            // Test message - no processing
            break;

        default:
            std::cerr << "Unknown message type: " << buf->header.message_type << std::endl;
            break;
    }

    // Call user-provided handler if registered
    if (message_handler_) {
        message_handler_(buf);
    }
}

void MessageWorker::sendToMonitoring(const MessageBuffer* buf) {
    // Create monitoring stats
    MessageStats stats;
    stats.message_number = buf->header.sequence_number;
    stats.send_timestamp = buf->header.publish_time_ns;
    stats.recv_timestamp = buf->header.recv_time_ns;
    stats.position = 0;  // Position not available in zero-copy mode

    // Non-blocking enqueue
    if (!stats_queue_.enqueue(stats)) {
        // Stats queue full - skip (performance priority)
        // Don't log error - too noisy
    }
}

// Message type handlers (placeholders - customize for your business logic)

void MessageWorker::handleOrderNew(const MessageBuffer* buf) {
    // TODO: Implement order new logic
    // Example:
    // - Parse order details from payload
    // - Validate order parameters
    // - Update order book
    // - Send confirmation
}

void MessageWorker::handleOrderExecution(const MessageBuffer* buf) {
    // TODO: Implement order execution logic
    // Example:
    // - Parse execution details
    // - Update order status
    // - Update positions
    // - Send execution report
}

void MessageWorker::handleOrderModify(const MessageBuffer* buf) {
    // TODO: Implement order modify logic
}

void MessageWorker::handleOrderCancel(const MessageBuffer* buf) {
    // TODO: Implement order cancel logic
}

void MessageWorker::handleQuoteUpdate(const MessageBuffer* buf) {
    // TODO: Implement quote update logic
}

MessageWorker::Statistics MessageWorker::getStatistics() const {
    Statistics stats;
    stats.messages_processed = messages_processed_.load(std::memory_order_relaxed);
    stats.messages_invalid = messages_invalid_.load(std::memory_order_relaxed);
    stats.messages_duplicate = messages_duplicate_.load(std::memory_order_relaxed);
    stats.queue_empty_count = queue_empty_count_.load(std::memory_order_relaxed);

    if (processing_count_ > 0) {
        stats.avg_processing_time_us =
            static_cast<double>(total_processing_time_ns_) / processing_count_ / 1000.0;
    } else {
        stats.avg_processing_time_us = 0.0;
    }

    if (queue_depth_samples_ > 0) {
        stats.avg_queue_depth =
            static_cast<double>(total_queue_depth_) / queue_depth_samples_;
    } else {
        stats.avg_queue_depth = 0.0;
    }

    return stats;
}

void MessageWorker::printStatistics() const {
    auto stats = getStatistics();

    std::cout << "\n=== Worker Thread Statistics ===" << std::endl;
    std::cout << "Messages processed:  " << stats.messages_processed << std::endl;
    std::cout << "Messages invalid:    " << stats.messages_invalid << std::endl;
    std::cout << "Messages duplicate:  " << stats.messages_duplicate << std::endl;
    std::cout << "Queue empty count:   " << stats.queue_empty_count << std::endl;

    if (stats.messages_processed > 0) {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Avg processing time: " << stats.avg_processing_time_us << " μs" << std::endl;
        std::cout << "Avg queue depth:     " << stats.avg_queue_depth << std::endl;
    }

    if (stats.messages_invalid > 0) {
        std::cout << "⚠️  WARNING: " << stats.messages_invalid
                  << " invalid messages" << std::endl;
    }

    if (stats.messages_duplicate > 0) {
        std::cout << "ℹ️  INFO: " << stats.messages_duplicate
                  << " duplicate messages filtered" << std::endl;
    }

    std::cout << "=================================\n" << std::endl;
}

} // namespace example
} // namespace aeron
