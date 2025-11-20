#include "AeronSubscriber.h"
#include "AeronConfig.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

namespace aeron {
namespace example {

AeronSubscriber::AeronSubscriber()
    : running_(false)
    , message_count_(0)
    , latency_sum_(0.0)
    , latency_min_(0.0)
    , latency_max_(0.0)
    , latency_count_(0)
    , last_message_number_(-1)
    , gap_count_(0)
    , total_gaps_(0)
    , buffer_pool_(nullptr)
    , message_queue_(nullptr)
    , zero_copy_enabled_(false)
    , zc_messages_received_(0)
    , zc_buffer_allocation_failures_(0)
    , zc_queue_full_failures_(0) {
}

AeronSubscriber::AeronSubscriber(const SubscriberConfig& config)
    : config_(config)
    , running_(false)
    , message_count_(0)
    , latency_sum_(0.0)
    , latency_min_(0.0)
    , latency_max_(0.0)
    , latency_count_(0)
    , last_message_number_(-1)
    , gap_count_(0)
    , total_gaps_(0)
    , buffer_pool_(nullptr)
    , message_queue_(nullptr)
    , zero_copy_enabled_(false)
    , zc_messages_received_(0)
    , zc_buffer_allocation_failures_(0)
    , zc_queue_full_failures_(0) {
}

AeronSubscriber::~AeronSubscriber() {
    shutdown();
}

void AeronSubscriber::setMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

void AeronSubscriber::enableZeroCopyMode(MessageBufferPool* pool, MessageBufferQueue* queue) {
    if (!pool || !queue) {
        std::cerr << "ERROR: Invalid buffer pool or message queue" << std::endl;
        return;
    }

    buffer_pool_ = pool;
    message_queue_ = queue;
    zero_copy_enabled_ = true;

    std::cout << "Zero-copy mode ENABLED" << std::endl;
    std::cout << "  Buffer pool capacity: " << pool->capacity() << std::endl;
    std::cout << "  Message queue capacity: " << queue->capacity() << std::endl;
}

void AeronSubscriber::disableZeroCopyMode() {
    zero_copy_enabled_ = false;
    buffer_pool_ = nullptr;
    message_queue_ = nullptr;

    std::cout << "Zero-copy mode DISABLED" << std::endl;
}

bool AeronSubscriber::isZeroCopyModeEnabled() const {
    return zero_copy_enabled_;
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
    // Extract message number from "Message 123 at ..." format
    size_t msg_pos = message.find("Message ");
    if (msg_pos == std::string::npos) {
        return -1;
    }

    size_t num_start = msg_pos + 8;  // Length of "Message "
    size_t num_end = message.find(" ", num_start);

    if (num_end == std::string::npos) {
        return -1;
    }

    try {
        return std::stoll(message.substr(num_start, num_end - num_start));
    } catch (const std::exception&) {
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

    // 4. Enqueue to worker thread (~50ns)
    if (!message_queue_->enqueue(msg_buf)) {
        // Queue full - return buffer to pool and drop message
        buffer_pool_->deallocate(msg_buf);
        zc_queue_full_failures_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // 5. Update statistics
    zc_messages_received_.fetch_add(1, std::memory_order_relaxed);

    // 6. Update checkpoint (if enabled) (~10ns)
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

    // If zero-copy mode is enabled, use fast path
    if (zero_copy_enabled_) {
        handleMessageFastPath(buffer, length, position);
        return;
    }

    // Legacy path (current implementation)
    // ① 수신 즉시 타임스탬프 기록 (최우선!)
    auto recv_now = std::chrono::system_clock::now().time_since_epoch();
    auto recv_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(recv_now).count();

    message_count_++;

    // ② 메시지에서 전송 타임스탬프 및 시퀀스 번호 추출
    std::string message(reinterpret_cast<const char*>(buffer), length);
    long long send_timestamp = 0;

    // Extract message number for gap detection
    int64_t msg_number = extractMessageNumber(message);

    // Gap detection
    if (msg_number >= 0) {
        if (last_message_number_ >= 0 && msg_number != last_message_number_ + 1) {
            int64_t gap_size = msg_number - last_message_number_ - 1;
            gap_count_++;
            total_gaps_ += gap_size;

            std::cerr << "\n⚠️  GAP DETECTED!" << std::endl;
            std::cerr << "  Last message: " << last_message_number_ << std::endl;
            std::cerr << "  Current message: " << msg_number << std::endl;
            std::cerr << "  Gap size: " << gap_size << " messages" << std::endl;
            std::cerr << "  Total gaps: " << gap_count_ << " (" << total_gaps_ << " messages)\n" << std::endl;
        }
        last_message_number_ = msg_number;
    }

    // "Message 123 at 1234567890" 형식에서 타임스탬프 추출
    size_t at_pos = message.find(" at ");
    if (at_pos != std::string::npos) {
        send_timestamp = std::stoll(message.substr(at_pos + 4));

        // ③ 레이턴시 계산 (마이크로초)
        double latency_us = (recv_timestamp - send_timestamp) / 1000.0;

        // ④ 통계 수집
        latency_sum_ += latency_us;
        latency_count_++;

        if (latency_min_ == 0.0 || latency_us < latency_min_) {
            latency_min_ = latency_us;
        }
        if (latency_us > latency_max_) {
            latency_max_ = latency_us;
        }

        // ⑤ 주기적으로 통계 출력 (1000개마다)
        if (message_count_ % 1000 == 0) {
            printLatencyStats();
            printGapStats();
        }
    } else {
        // 타임스탬프가 없는 메시지 (로깅만)
        if (message_count_ % 1000 == 0) {
            std::cout << "Received " << message_count_
                      << " messages at position " << position << std::endl;
            printGapStats();
        }
    }

    // ⑥ 모니터링 콜백 호출 (성능 영향 최소화)
    if (message_callback_) {
        message_callback_(
            msg_number,
            send_timestamp,
            recv_timestamp,
            position
        );
    }

    // ⑦ Checkpoint 업데이트 (if enabled) (~10ns)
    if (checkpoint_ && msg_number >= 0) {
        checkpoint_->update(msg_number, position, message_count_);
    }
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

void AeronSubscriber::printLatencyStats() {
    if (latency_count_ == 0) {
        return;
    }

    double avg_latency = latency_sum_ / latency_count_;

    std::cout << "\n========================================" << std::endl;
    std::cout << "Latency Statistics (" << latency_count_ << " samples)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Min:     " << std::fixed << std::setprecision(2) << latency_min_ << " μs" << std::endl;
    std::cout << "Max:     " << latency_max_ << " μs" << std::endl;
    std::cout << "Average: " << avg_latency << " μs" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void AeronSubscriber::printGapStats() {
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Gap Statistics" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Total messages received: " << message_count_ << std::endl;
    std::cout << "Last message number: " << last_message_number_ << std::endl;
    std::cout << "Gaps detected: " << gap_count_ << std::endl;
    std::cout << "Total missing messages: " << total_gaps_ << std::endl;

    if (last_message_number_ > 0) {
        double loss_rate = (double)total_gaps_ / (last_message_number_ + 1) * 100.0;
        std::cout << "Message loss rate: " << std::fixed << std::setprecision(2) << loss_rate << "%" << std::endl;
    }
    std::cout << "----------------------------------------\n" << std::endl;
}

void AeronSubscriber::shutdown() {
    std::cout << "Shutting down Subscriber..." << std::endl;

    running_ = false;

    // 최종 통계 출력
    if (latency_count_ > 0 || gap_count_ > 0) {
        std::cout << "\n=== FINAL STATISTICS ===" << std::endl;
        if (latency_count_ > 0) {
            printLatencyStats();
        }
        printGapStats();
    }

    // ReplayMerge 정리 (자동으로 정리됨)
    replay_merge_.reset();
    subscription_.reset();
    archive_.reset();
    aeron_.reset();

    std::cout << "Subscriber shutdown complete. Total messages: "
              << message_count_ << std::endl;
}

} // namespace example
} // namespace aeron
