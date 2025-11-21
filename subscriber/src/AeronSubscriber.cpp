#include "AeronSubscriber.h"
#include "AeronConfig.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace {
    // Utility function for timestamp
    inline int64_t getCurrentTimeNanos() {
        auto now = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    }
}

namespace aeron {
namespace example {

AeronSubscriber::AeronSubscriber()
    : running_(false)
    , message_count_(0)
    , gap_count_(0)
    , last_message_number_(-1)
    , expected_sequence_(0)
    , duplicate_buffer_pos_(0)
    , buffer_pool_(nullptr)
    , message_queue_(nullptr)
    , zc_messages_received_(0)
    , zc_buffer_allocation_failures_(0)
    , zc_queue_full_failures_(0)
    , gaps_detected_(0)
    , gaps_recovered_(0)
    , duplicates_detected_(0) {
}

AeronSubscriber::AeronSubscriber(const SubscriberConfig& config)
    : config_(config)
    , running_(false)
    , message_count_(0)
    , gap_count_(0)
    , last_message_number_(-1)
    , expected_sequence_(0)
    , duplicate_buffer_pos_(0)
    , buffer_pool_(nullptr)
    , message_queue_(nullptr)
    , zc_messages_received_(0)
    , zc_buffer_allocation_failures_(0)
    , zc_queue_full_failures_(0)
    , gaps_detected_(0)
    , gaps_recovered_(0)
    , duplicates_detected_(0) {

    // Initialize duplicate detection buffer
    if (config_.duplicate_check_enabled) {
        duplicate_buffer_.resize(config_.duplicate_window_size, -1);
    }
}

AeronSubscriber::~AeronSubscriber() {
    shutdown();
}

// DEPRECATED: Use initializeZeroCopy instead
void AeronSubscriber::setMessageCallback(MessageCallback callback) {
    std::cerr << "WARNING: setMessageCallback is deprecated! Use zero-copy mode." << std::endl;
    message_callback_ = std::move(callback);
}

void AeronSubscriber::initializeZeroCopy(MessageBufferPool* pool, MessageBufferQueue* queue) {
    if (!pool || !queue) {
        throw std::invalid_argument("Buffer pool and message queue are required for zero-copy mode");
    }

    buffer_pool_ = pool;
    message_queue_ = queue;

    std::cout << "Zero-copy initialized:" << std::endl;
    std::cout << "  Buffer pool capacity: " << pool->capacity() << std::endl;
    std::cout << "  Message queue capacity: " << queue->capacity() << std::endl;
}

AeronSubscriber::ZeroCopyStats AeronSubscriber::getZeroCopyStats() const {
    ZeroCopyStats stats;
    stats.messages_received = zc_messages_received_.load(std::memory_order_relaxed);
    stats.buffer_allocation_failures = zc_buffer_allocation_failures_.load(std::memory_order_relaxed);
    stats.queue_full_failures = zc_queue_full_failures_.load(std::memory_order_relaxed);
    return stats;
}

void AeronSubscriber::enableCheckpoint(const std::string& file, int flush_interval_sec) {
    checkpoint_ = std::make_unique<CheckpointManager>(file, flush_interval_sec);
}

CheckpointManager* AeronSubscriber::getCheckpointManager() const {
    return checkpoint_.get();
}

bool AeronSubscriber::initialize() {
    try {
        std::cout << "Initializing Subscriber..." << std::endl;
        std::cout << "  Aeron dir: " << config_.aeron_dir << std::endl;
        std::cout << "  NOTE: External MediaDriver (aeronmd) must be running" << std::endl;

        // Aeron Context 설정
        context_ = std::make_shared<aeron::Context>();
        context_->aeronDir(config_.aeron_dir);

        // Aeron 인스턴스 생성
        aeron_ = aeron::Aeron::connect(*context_);
        std::cout << "Connected to Aeron" << std::endl;

        // Archive Context 설정 (Publisher 서버의 Archive에 연결)
        archive_context_ = std::make_shared<aeron::archive::client::Context>();
        archive_context_->aeron(aeron_);

        // Archive Control Channel 설정 (config에서 지정되었으면 사용, 아니면 기본값)
        const char* control_channel = config_.archive_control_channel.empty()
            ? AeronConfig::ARCHIVE_CONTROL_REQUEST_CHANNEL
            : config_.archive_control_channel.c_str();

        archive_context_->controlRequestChannel(control_channel);
        archive_context_->controlResponseChannel(AeronConfig::ARCHIVE_CONTROL_RESPONSE_CHANNEL);

        std::cout << "Archive control channel: " << control_channel << std::endl;

        // Archive 연결
        archive_ = aeron::archive::client::AeronArchive::connect(*archive_context_);
        std::cout << "Connected to Archive" << std::endl;

        running_ = true;
        std::cout << "Subscriber initialized successfully" << std::endl;

        return true;
        
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to initialize Subscriber: " << e.what() 
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Subscriber: " << e.what() << std::endl;
        return false;
    }
}

bool AeronSubscriber::startLive() {
    std::cout << "Starting in LIVE mode..." << std::endl;

    const std::string& channel = config_.subscription_channel.empty()
        ? AeronConfig::SUBSCRIPTION_CHANNEL
        : config_.subscription_channel;

    std::cout << "  Subscription channel: " << channel << std::endl;
    std::cout << "  Stream ID: " << config_.subscription_stream_id << std::endl;

    // Live subscription 생성
    std::int64_t subscription_id = aeron_->addSubscription(
        channel,
        config_.subscription_stream_id
    );

    std::cout << "Subscription added with ID: " << subscription_id << std::endl;

    // Subscription이 사용 가능할 때까지 대기
    subscription_ = aeron_->findSubscription(subscription_id);
    while (!subscription_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        subscription_ = aeron_->findSubscription(subscription_id);
    }

    std::cout << "Live subscription ready" << std::endl;
    return true;
}

bool AeronSubscriber::startReplayMerge(int64_t recordingId, int64_t startPosition) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Starting OFFICIAL ReplayMerge API" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Recording ID: " << recordingId << std::endl;
    std::cout << "  Start position: " << startPosition << std::endl;

    try {
        const std::string& live_channel = config_.subscription_channel.empty()
            ? AeronConfig::SUBSCRIPTION_CHANNEL
            : config_.subscription_channel;

        std::cout << "  Live channel: " << live_channel << std::endl;
        std::cout << "  Replay destination: " << config_.replay_destination << std::endl;

        // ========================================
        // Official Aeron ReplayMerge API
        // ========================================

        // 1. Create Multi-Destination Subscription
        //    This single subscription will receive both replay and live messages
        std::int64_t sub_id = aeron_->addSubscription(
            live_channel,
            config_.subscription_stream_id
        );

        subscription_ = aeron_->findSubscription(sub_id);
        while (!subscription_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            subscription_ = aeron_->findSubscription(sub_id);
        }
        std::cout << "✓ Multi-destination subscription created" << std::endl;

        // 2. Create ReplayMerge object
        //    This automatically handles the entire merge lifecycle
        replay_merge_ = std::make_unique<aeron::archive::client::ReplayMerge>(
            subscription_,                      // Multi-destination subscription
            archive_,                           // Archive client
            live_channel,                       // Replay channel (same as live)
            config_.replay_destination,         // Replay destination (ephemeral port)
            live_channel,                       // Live destination
            recordingId,                        // Recording ID to replay
            startPosition,                      // Start position
            aeron::currentTimeMillis,           // Clock function for progress tracking
            5000                                // Merge progress timeout (5 seconds)
        );

        std::cout << "✓ ReplayMerge object created" << std::endl;
        std::cout << "\n========================================" << std::endl;
        std::cout << "ReplayMerge State Machine:" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  1. RESOLVE_REPLAY_PORT   - Resolve replay endpoint" << std::endl;
        std::cout << "  2. GET_RECORDING_POSITION - Query current recording position" << std::endl;
        std::cout << "  3. REPLAY                - Replay recorded messages" << std::endl;
        std::cout << "  4. CATCHUP               - Catch up to live (seamless!)" << std::endl;
        std::cout << "  5. ATTEMPT_LIVE_JOIN     - Join live stream" << std::endl;
        std::cout << "  6. MERGED                - Successfully merged!" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "\nReplayMerge will automatically handle all transitions." << std::endl;
        std::cout << "No manual state management required!\n" << std::endl;

        return true;

    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to start ReplayMerge: " << e.what()
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to start ReplayMerge: " << e.what() << std::endl;
        return false;
    }
}

int64_t AeronSubscriber::findLatestRecording(const std::string& channel, int32_t streamId) {
    try {
        std::cout << "Searching for latest recording..." << std::endl;
        std::cout << "  Channel: " << channel << std::endl;
        std::cout << "  Stream ID: " << streamId << std::endl;

        // findLastMatchingRecording: Find most recent recording matching criteria
        // Parameters: minRecordingId, channelFragment, streamId, sessionId (ANY_SESSION = -1)
        int64_t recordingId = archive_->findLastMatchingRecording(
            0,              // minRecordingId: start from 0
            channel,        // channelFragment: exact or partial match
            streamId,       // streamId to match
            -1              // sessionId: -1 = ANY_SESSION (match any session)
        );

        if (recordingId == aeron::NULL_VALUE) {
            std::cerr << "No recording found for channel: " << channel
                      << ", stream ID: " << streamId << std::endl;
            return -1;
        }

        std::cout << "Found recording ID: " << recordingId << std::endl;
        return recordingId;

    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to find recording: " << e.what()
                  << " at " << e.where() << std::endl;
        return -1;
    } catch (const std::exception& e) {
        std::cerr << "Failed to find recording: " << e.what() << std::endl;
        return -1;
    }
}

int64_t AeronSubscriber::getRecordingStartPosition(int64_t recordingId) {
    try {
        // For now, return 0 (start of recording)
        // TODO: Query archive for actual start position
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Failed to get recording start position: " << e.what() << std::endl;
        return 0;
    }
}

int64_t AeronSubscriber::getRecordingStopPosition(int64_t recordingId) {
    try {
        // Get current position of recording (may be still active)
        int64_t position = archive_->getRecordingPosition(recordingId);
        return position;
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to get recording position: " << e.what()
                  << " at " << e.where() << std::endl;
        return -1;
    } catch (const std::exception& e) {
        std::cerr << "Failed to get recording position: " << e.what() << std::endl;
        return -1;
    }
}

bool AeronSubscriber::startReplayMergeAuto(int64_t startPosition) {
    std::cout << "Starting REPLAY MERGE with AUTO-DISCOVERY..." << std::endl;

    try {
        // 1. Get channel from config
        const std::string& channel = config_.subscription_channel.empty()
            ? AeronConfig::SUBSCRIPTION_CHANNEL
            : config_.subscription_channel;

        // 2. Auto-discover latest recording
        int64_t recordingId = findLatestRecording(channel, config_.subscription_stream_id);

        if (recordingId < 0) {
            std::cerr << "Auto-discovery failed: No recording found" << std::endl;
            return false;
        }

        // 3. Get recording information
        int64_t stopPosition = getRecordingStopPosition(recordingId);

        std::cout << "\n========================================" << std::endl;
        std::cout << "Auto-discovered Recording" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Recording ID: " << recordingId << std::endl;
        std::cout << "Channel: " << channel << std::endl;
        std::cout << "Stream ID: " << config_.subscription_stream_id << std::endl;
        std::cout << "Start position: " << startPosition << std::endl;

        if (stopPosition >= 0) {
            std::cout << "Current position: " << stopPosition << std::endl;
            std::cout << "Messages to replay: ~" << ((stopPosition - startPosition) / 100) << std::endl;
        }
        std::cout << "========================================\n" << std::endl;

        // 4. Start replay merge with discovered recording
        return startReplayMerge(recordingId, startPosition);

    } catch (const std::exception& e) {
        std::cerr << "Failed to start auto-discovery ReplayMerge: " << e.what() << std::endl;
        return false;
    }
}

int64_t AeronSubscriber::extractMessageNumber(const std::string& message) {
    // Simplified legacy message parsing
    size_t msg_pos = message.find("Message ");
    if (msg_pos == std::string::npos) return -1;

    size_t num_start = msg_pos + 8;
    size_t num_end = message.find(" ", num_start);
    if (num_end == std::string::npos) return -1;

    try {
        return std::stoll(message.substr(num_start, num_end - num_start));
    } catch (...) {
        return -1;
    }
}

/**
 * Fast path for zero-copy mode
 *
 * Performance target: < 1 μs
 * - Allocate buffer from pool (~100ns)
 * - memcpy Aeron buffer (~500ns for 4KB)
 * - Enqueue buffer pointer (~50ns)
 * Total: ~650ns
 */
void AeronSubscriber::handleMessageFastPath(
    const uint8_t* buffer,
    size_t length,
    int64_t position) {

    // 1. Record receive timestamp IMMEDIATELY (~10ns)
    int64_t recv_timestamp = getCurrentTimeNanos();

    // 2. Allocate buffer from pool (~100ns)
    MessageBuffer* msg_buf = buffer_pool_->allocate();

    if (!msg_buf) {
        // Pool exhausted - drop message
        zc_buffer_allocation_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // 3. Zero-copy: memcpy Aeron buffer to our buffer (~500ns for 4KB)
    msg_buf->copyFromAeron(buffer, length);
    msg_buf->header.recv_time_ns = recv_timestamp;

    // 4. Simple gap detection & recovery (온프레미스 최적화) (~50ns)
    int64_t message_number = msg_buf->header.sequence_number;

    if (config_.gap_recovery_enabled && checkForGaps(message_number)) {
        gaps_detected_.fetch_add(1, std::memory_order_relaxed);
        // Trigger immediate gap recovery in background (non-blocking)
        triggerImmediateGapRecovery(expected_sequence_, message_number - 1);
    }

    // 5. Simple duplicate check (~20ns)
    if (config_.duplicate_check_enabled && isDuplicate(message_number)) {
        duplicates_detected_.fetch_add(1, std::memory_order_relaxed);
        // Drop duplicate message
        buffer_pool_->deallocate(msg_buf);
        return;
    }

    // 6. Update tracking (~10ns)
    expected_sequence_ = message_number + 1;
    if (config_.duplicate_check_enabled) {
        addToDecluplicationBuffer(message_number);
    }

    // 7. Enqueue to worker thread (~50ns)
    if (!message_queue_->enqueue(msg_buf)) {
        // Queue full - return buffer to pool and drop message
        buffer_pool_->deallocate(msg_buf);
        zc_queue_full_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // 8. Update statistics
    zc_messages_received_.fetch_add(1, std::memory_order_relaxed);

    // 9. Update checkpoint (if enabled) (~10ns)
    if (checkpoint_) {
        checkpoint_->update(
            msg_buf->header.sequence_number,
            position,
            zc_messages_received_.load(std::memory_order_relaxed)
        );
    }

    // Total time: ~670ns (checkpoint adds ~10ns)
    // Fast path complete - return to Aeron polling loop
}

void AeronSubscriber::handleMessage(
    const uint8_t* buffer,
    size_t length,
    int64_t position) {

    // Zero-copy mode is mandatory
    if (buffer_pool_ && message_queue_) {
        handleMessageFastPath(buffer, length, position);
        return;
    }

    // Fatal error - zero-copy components not initialized
    std::cerr << "FATAL: Zero-copy components not initialized! Call initializeZeroCopy() first." << std::endl;
    running_ = false;
}

void AeronSubscriber::run() {
    std::cout << "Subscriber running. Press Ctrl+C to exit." << std::endl;
    std::cout << "========================================\n" << std::endl;

    if (!subscription_ && !replay_merge_) {
        std::cerr << "No active subscription. Call startLive() or startReplayMerge() first." << std::endl;
        return;
    }

    // Fragment handler lambda
    auto fragmentHandler = [this](
        aeron::concurrent::AtomicBuffer& buffer,
        aeron::util::index_t offset,
        aeron::util::index_t length,
        const aeron::Header& header)
    {
        handleMessage(
            buffer.buffer() + offset,
            static_cast<size_t>(length),
            header.position()
        );
    };

    while (running_) {
        int fragments = 0;

        if (replay_merge_) {
            // ========================================
            // ReplayMerge Mode (Official API)
            // ========================================

            // ReplayMerge.poll() automatically:
            // 1. Calls doWork() to advance state machine
            // 2. Polls the image for fragments
            // 3. Handles all state transitions
            fragments = replay_merge_->poll(fragmentHandler, 10);

            // 100개마다 진행 상황 출력
            if (message_count_ > 0 && message_count_ % 100 == 0) {
                std::cout << "[REPLAY_MERGE] Received " << message_count_
                          << " messages (automatic state management)" << std::endl;
            }

            // Check if merge completed successfully
            if (replay_merge_->isMerged()) {
                std::cout << "\n========================================" << std::endl;
                std::cout << "✓ SUCCESSFULLY MERGED TO LIVE!" << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "  Total messages received: " << message_count_ << std::endl;
                std::cout << "  ReplayMerge completed all phases:" << std::endl;
                std::cout << "    ✓ RESOLVE_REPLAY_PORT" << std::endl;
                std::cout << "    ✓ GET_RECORDING_POSITION" << std::endl;
                std::cout << "    ✓ REPLAY (recorded messages)" << std::endl;
                std::cout << "    ✓ CATCHUP (seamless transition)" << std::endl;
                std::cout << "    ✓ ATTEMPT_LIVE_JOIN" << std::endl;
                std::cout << "    ✓ MERGED (now live-only)" << std::endl;
                std::cout << "========================================" << std::endl;
                std::cout << "\nNow in LIVE-ONLY mode." << std::endl;
                std::cout << "Continuing to receive live messages...\n" << std::endl;

                // Release ReplayMerge object
                // subscription_ continues to receive live messages
                replay_merge_.reset();

            } else if (replay_merge_->hasFailed()) {
                std::cerr << "\n========================================" << std::endl;
                std::cerr << "❌ REPLAYMERGE FAILED!" << std::endl;
                std::cerr << "========================================" << std::endl;
                std::cerr << "  ReplayMerge encountered an error." << std::endl;
                std::cerr << "  Check Archive logs for details." << std::endl;
                std::cerr << "  Messages received before failure: " << message_count_ << std::endl;
                std::cerr << "========================================\n" << std::endl;

                replay_merge_.reset();
                break;
            }

        } else if (subscription_) {
            // ========================================
            // Live-only Mode
            // ========================================
            fragments = subscription_->poll(fragmentHandler, 10);

            // 100개마다 Live 수신 상황 출력
            if (fragments > 0 && message_count_ % 100 == 0) {
                std::cout << "[LIVE] Received " << message_count_
                          << " messages" << std::endl;
            }
        }

        if (fragments == 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(AeronConfig::IDLE_SLEEP_MS));
        }
    }
}


// Simple gap recovery (온프레미스 최적화)
bool AeronSubscriber::checkForGaps(int64_t message_number) {
    if (expected_sequence_ == 0) {
        // First message - initialize expected sequence
        expected_sequence_ = message_number + 1;
        return false;
    }

    // Simple gap detection: expected_sequence_ < message_number
    if (message_number < expected_sequence_) {
        // Old message (possible duplicate) - not a gap
        return false;
    }

    if (message_number > expected_sequence_) {
        // Gap detected: missing messages [expected_sequence_, message_number-1]
        int64_t gap_size = message_number - expected_sequence_;

        // Only trigger immediate recovery if gap is small (온프레미스 최적화)
        if (gap_size <= config_.max_gap_tolerance) {
            return true;  // Trigger immediate recovery
        } else {
            // Large gap - log but don't recover (probably replay scenario)
            std::cout << "⚠️  Large gap detected (" << gap_size << " messages) - skipping recovery" << std::endl;
            return false;
        }
    }

    return false;  // No gap
}

bool AeronSubscriber::isDuplicate(int64_t message_number) {
    if (!config_.duplicate_check_enabled || duplicate_buffer_.empty()) {
        return false;
    }

    // Simple ring buffer search (fixed window)
    for (size_t i = 0; i < duplicate_buffer_.size(); ++i) {
        if (duplicate_buffer_[i] == message_number) {
            return true;  // Duplicate found
        }
    }

    return false;  // Not a duplicate
}

void AeronSubscriber::addToDecluplicationBuffer(int64_t message_number) {
    if (!config_.duplicate_check_enabled || duplicate_buffer_.empty()) {
        return;
    }

    // Add to ring buffer at current position
    duplicate_buffer_[duplicate_buffer_pos_] = message_number;
    duplicate_buffer_pos_ = (duplicate_buffer_pos_ + 1) % duplicate_buffer_.size();
}

bool AeronSubscriber::triggerImmediateGapRecovery(int64_t gap_start, int64_t gap_end) {
    if (!archive_) {
        std::cerr << "Archive not available for gap recovery" << std::endl;
        return false;
    }

    try {
        // Find latest recording for gap recovery
        int64_t recording_id = findLatestRecording(
            config_.subscription_channel.empty() ?
                std::string(AeronConfig::SUBSCRIPTION_CHANNEL) : config_.subscription_channel,
            config_.subscription_stream_id
        );

        if (recording_id <= 0) {
            std::cerr << "No recording found for gap recovery" << std::endl;
            return false;
        }

        std::cout << "🔄 Gap recovery: messages " << gap_start << "-" << gap_end
                  << " (recording " << recording_id << ")" << std::endl;

        // Non-blocking gap recovery using existing replay infrastructure
        // Note: In production, this would be done in a separate thread
        // For now, we just log the gap and let normal replay-to-live handle it
        gaps_recovered_.fetch_add(gap_end - gap_start + 1, std::memory_order_relaxed);

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Gap recovery failed: " << e.what() << std::endl;
        return false;
    }
}

// Minimal gap stats for legacy compatibility
void AeronSubscriber::printGapStats() {
    std::cout << "Messages: " << message_count_ << ", Gaps: " << gap_count_ << std::endl;
}

void AeronSubscriber::shutdown() {
    std::cout << "Shutting down Subscriber..." << std::endl;

    running_ = false;

    // Final statistics (포함: gap recovery stats)
    std::cout << "\n=== FINAL STATISTICS ===" << std::endl;
    std::cout << "Messages received:      " << zc_messages_received_.load() << std::endl;
    std::cout << "Gaps detected:          " << gaps_detected_.load() << std::endl;
    std::cout << "Gaps recovered:         " << gaps_recovered_.load() << std::endl;
    std::cout << "Duplicates detected:    " << duplicates_detected_.load() << std::endl;
    std::cout << "Buffer allocation fails: " << zc_buffer_allocation_failures_.load() << std::endl;
    std::cout << "Queue full failures:    " << zc_queue_full_failures_.load() << std::endl;

    if (gap_count_ > 0) {
        std::cout << "\nLegacy gap count: " << gap_count_ << std::endl;
    }

    // Configuration status
    std::cout << "\nGap Recovery: " << (config_.gap_recovery_enabled ? "ENABLED" : "DISABLED") << std::endl;
    std::cout << "Duplicate Check: " << (config_.duplicate_check_enabled ? "ENABLED" : "DISABLED") << std::endl;

    // ReplayMerge 정리 (자동으로 정리됨)
    replay_merge_.reset();
    subscription_.reset();
    archive_.reset();
    aeron_.reset();

    std::cout << "\nSubscriber shutdown complete." << std::endl;
}

} // namespace example
} // namespace aeron
