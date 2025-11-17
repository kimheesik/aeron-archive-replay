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
    , replay_session_id_(-1)
    , is_replay_merge_active_(false)
    , is_replay_complete_(false)
    , replay_message_count_(0)
    , live_message_count_(0)
    , latency_sum_(0.0)
    , latency_min_(0.0)
    , latency_max_(0.0)
    , latency_count_(0)
    , last_message_number_(-1)
    , gap_count_(0)
    , total_gaps_(0) {
}

AeronSubscriber::AeronSubscriber(const SubscriberConfig& config)
    : config_(config)
    , running_(false)
    , message_count_(0)
    , replay_session_id_(-1)
    , is_replay_merge_active_(false)
    , is_replay_complete_(false)
    , replay_message_count_(0)
    , live_message_count_(0)
    , latency_sum_(0.0)
    , latency_min_(0.0)
    , latency_max_(0.0)
    , latency_count_(0)
    , last_message_number_(-1)
    , gap_count_(0)
    , total_gaps_(0) {
}

AeronSubscriber::~AeronSubscriber() {
    shutdown();
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
    live_subscription_ = aeron_->findSubscription(subscription_id);
    while (!live_subscription_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        live_subscription_ = aeron_->findSubscription(subscription_id);
    }

    std::cout << "Live subscription ready" << std::endl;
    return true;
}

bool AeronSubscriber::startReplayMerge(int64_t recordingId, int64_t startPosition) {
    std::cout << "Starting REPLAY MERGE mode with Aeron Archive API..." << std::endl;
    std::cout << "  Recording ID: " << recordingId << std::endl;
    std::cout << "  Start position: " << startPosition << std::endl;

    try {
        const std::string& live_channel = config_.subscription_channel.empty()
            ? AeronConfig::SUBSCRIPTION_CHANNEL
            : config_.subscription_channel;

        std::cout << "  Live channel: " << live_channel << std::endl;
        std::cout << "  Replay destination: " << config_.replay_destination << std::endl;

        // ========================================
        // ReplayMerge Pattern using Archive API
        // ========================================

        // 1. Create LIVE subscription first
        //    This will receive live messages during and after replay
        std::int64_t live_sub_id = aeron_->addSubscription(
            live_channel,
            config_.subscription_stream_id
        );

        live_subscription_ = aeron_->findSubscription(live_sub_id);
        while (!live_subscription_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            live_subscription_ = aeron_->findSubscription(live_sub_id);
        }
        std::cout << "✓ Live subscription created" << std::endl;

        // 2. Create REPLAY subscription
        //    Replay data will be sent to replay destination
        std::int64_t replay_sub_id = aeron_->addSubscription(
            config_.replay_destination,
            config_.subscription_stream_id
        );

        replay_subscription_ = aeron_->findSubscription(replay_sub_id);
        while (!replay_subscription_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            replay_subscription_ = aeron_->findSubscription(replay_sub_id);
        }
        std::cout << "✓ Replay subscription created" << std::endl;

        // 3. Start Replay using Archive API
        //    Archive will send recording data to replay destination
        replay_session_id_ = archive_->startReplay(
            recordingId,                      // Recording to replay
            startPosition,                    // Start position
            INT64_MAX,                        // Length (to end)
            config_.replay_destination,       // Replay destination channel
            config_.subscription_stream_id    // Stream ID
        );

        is_replay_merge_active_ = true;
        is_replay_complete_ = false;

        std::cout << "✓ Replay started (session ID: " << replay_session_id_ << ")" << std::endl;
        std::cout << "\n========================================" << std::endl;
        std::cout << "ReplayMerge Active" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Phase 1: REPLAY - Receiving recorded messages" << std::endl;
        std::cout << "Phase 2: TRANSITION - Replay completes" << std::endl;
        std::cout << "Phase 3: LIVE - Receiving live messages only" << std::endl;
        std::cout << "========================================\n" << std::endl;

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

void AeronSubscriber::handleMessage(
    const uint8_t* buffer,
    size_t length,
    int64_t position) {

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
            std::string mode_str = is_replay_merge_active_ ? "REPLAY_MERGE" : "LIVE";
            std::cout << "[" << mode_str << "] Received " << message_count_
                      << " messages at position " << position << std::endl;
            printGapStats();
        }
    }
}

void AeronSubscriber::run() {
    std::cout << "Subscriber running. Press Ctrl+C to exit." << std::endl;
    std::cout << "========================================\n" << std::endl;

    if (!live_subscription_ && !replay_subscription_) {
        std::cerr << "No active subscription. Call startLive() or startReplayMerge() first." << std::endl;
        return;
    }

    // Fragment handler lambda - Replay용
    auto replayFragmentHandler = [this](
        aeron::concurrent::AtomicBuffer& buffer,
        aeron::util::index_t offset,
        aeron::util::index_t length,
        const aeron::Header& header)
    {
        replay_message_count_++;
        handleMessage(
            buffer.buffer() + offset,
            static_cast<size_t>(length),
            header.position()
        );
    };

    // Fragment handler lambda - Live용
    auto liveFragmentHandler = [this](
        aeron::concurrent::AtomicBuffer& buffer,
        aeron::util::index_t offset,
        aeron::util::index_t length,
        const aeron::Header& header)
    {
        live_message_count_++;
        handleMessage(
            buffer.buffer() + offset,
            static_cast<size_t>(length),
            header.position()
        );
    };

    while (running_) {
        int fragments = 0;

        if (is_replay_merge_active_) {
            // ========================================
            // ReplayMerge Mode
            // ========================================

            // Poll replay subscription (for replayed data)
            if (!is_replay_complete_ && replay_subscription_) {
                int replay_fragments = replay_subscription_->poll(replayFragmentHandler, 10);
                fragments += replay_fragments;

                // 100개마다 Replay 진행 상황 출력
                if (replay_message_count_ > 0 && replay_message_count_ % 100 == 0) {
                    std::cout << "[REPLAY] Received " << replay_message_count_
                              << " messages from recording" << std::endl;
                }

                // Check if replay is complete (no more images)
                if (replay_subscription_->imageCount() == 0 && !is_replay_complete_) {
                    is_replay_complete_ = true;
                    std::cout << "\n========================================" << std::endl;
                    std::cout << "✓ REPLAY COMPLETE" << std::endl;
                    std::cout << "  Total replay messages: " << replay_message_count_ << std::endl;
                    std::cout << "  Transitioning to LIVE mode..." << std::endl;
                    std::cout << "========================================\n" << std::endl;

                    // Clean up replay subscription
                    replay_subscription_.reset();

                    // Optionally stop replay session
                    if (replay_session_id_ >= 0) {
                        try {
                            archive_->stopReplay(replay_session_id_);
                        } catch (const std::exception& e) {
                            std::cerr << "Warning: Failed to stop replay: " << e.what() << std::endl;
                        }
                    }
                }
            }

            // Always poll live subscription (may receive live data during replay)
            if (live_subscription_) {
                int live_fragments = live_subscription_->poll(liveFragmentHandler, 10);
                fragments += live_fragments;

                // ReplayMerge 중에도 Live 메시지 수신 시 출력
                if (live_fragments > 0 && live_message_count_ % 100 == 0) {
                    std::cout << "[LIVE during REPLAY] Received " << live_message_count_
                              << " live messages" << std::endl;
                }
            }

            // If replay is complete, transition to live-only mode
            if (is_replay_complete_) {
                is_replay_merge_active_ = false;
                std::cout << "\n========================================" << std::endl;
                std::cout << "✓ Now in LIVE-ONLY mode" << std::endl;
                std::cout << "  Total replay messages: " << replay_message_count_ << std::endl;
                std::cout << "  Live messages during replay: " << live_message_count_ << std::endl;
                std::cout << "  Now receiving live messages only..." << std::endl;
                std::cout << "========================================\n" << std::endl;
            }

        } else {
            // ========================================
            // Live-only Mode
            // ========================================
            if (live_subscription_) {
                int live_fragments = live_subscription_->poll(liveFragmentHandler, 10);
                fragments += live_fragments;

                // 100개마다 Live 수신 상황 출력
                if (live_fragments > 0 && live_message_count_ % 100 == 0) {
                    std::cout << "[LIVE] Received " << live_message_count_
                              << " messages (Total: " << message_count_ << ")" << std::endl;
                }
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

    // ReplayMerge 정리
    if (replay_session_id_ >= 0 && archive_) {
        try {
            archive_->stopReplay(replay_session_id_);
        } catch (const std::exception& e) {
            // Ignore errors during shutdown
        }
    }

    replay_subscription_.reset();
    live_subscription_.reset();
    archive_.reset();
    aeron_.reset();

    std::cout << "Subscriber shutdown complete. Total messages: "
              << message_count_ << std::endl;
}

} // namespace example
} // namespace aeron
