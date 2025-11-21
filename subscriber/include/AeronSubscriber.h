#ifndef AERON_SUBSCRIBER_H
#define AERON_SUBSCRIBER_H

#include <memory>
#include <atomic>
#include <string>
#include <functional>
#include "Aeron.h"
#include "client/AeronArchive.h"
#include "client/ReplayMerge.h"
#include "BufferPool.h"
#include "MessageQueue.h"
#include "CheckpointManager.h"

namespace aeron {
namespace example {

struct SubscriberConfig {
    std::string aeron_dir = "/home/hesed/shm/aeron-subscriber";
    std::string archive_control_channel = "";  // 비어있으면 AeronConfig 사용
    std::string subscription_channel = "";     // 비어있으면 AeronConfig 사용
    int subscription_stream_id = 10;           // 기본값
    std::string replay_destination = "aeron:udp?endpoint=localhost:40457";  // ReplayMerge destination

    // Gap Recovery Configuration (온프레미스 환경 최적화)
    bool gap_recovery_enabled = true;          // Gap 복구 활성화 여부
    int64_t max_gap_tolerance = 5;             // 허용 가능한 최대 Gap 개수 (즉시 복구)
    int gap_recovery_timeout_ms = 1000;        // Gap 복구 타임아웃 (ms)
    bool duplicate_check_enabled = true;       // 중복 체크 활성화
    int64_t duplicate_window_size = 1000;      // 중복 체크 윈도우 크기

    SubscriberConfig() = default;
};

class AeronSubscriber {
public:
    // Legacy callback type - deprecated, use zero-copy mode instead
    using MessageCallback = std::function<void(int64_t, int64_t, int64_t, int64_t)>;

    AeronSubscriber();
    explicit AeronSubscriber(const SubscriberConfig& config);
    ~AeronSubscriber();

    bool initialize();
    bool startLive();
    bool startReplayMerge(int64_t recordingId, int64_t startPosition);
    bool startReplayMergeAuto(int64_t startPosition = 0);  // Auto-discover latest recording
    void run();
    void shutdown();

    // DEPRECATED: Use zero-copy mode for better performance
    void setMessageCallback(MessageCallback callback);

    /**
     * Initialize zero-copy processing
     *
     * Zero-copy mode is now the default and only mode:
     * - Buffer pool and message queue are required
     * - Aeron messages are copied to buffer pool
     * - Buffer pointers are enqueued to message queue
     * - Worker thread dequeues and processes messages
     *
     * @param pool Buffer pool (external, not owned)
     * @param queue Message queue (external, not owned)
     */
    void initializeZeroCopy(MessageBufferPool* pool, MessageBufferQueue* queue);

    /**
     * Get statistics for zero-copy mode
     */
    struct ZeroCopyStats {
        uint64_t messages_received;
        uint64_t buffer_allocation_failures;
        uint64_t queue_full_failures;
    };

    ZeroCopyStats getZeroCopyStats() const;

    /**
     * Enable checkpoint persistence
     *
     * This enables automatic checkpoint saving with minimal overhead:
     * - Main thread: ~10 ns per update (atomic stores only)
     * - Background thread: Flushes to disk every N seconds
     *
     * @param file Checkpoint file path
     * @param flush_interval_sec Flush interval in seconds (default: 1)
     */
    void enableCheckpoint(const std::string& file, int flush_interval_sec = 1);

    /**
     * Get checkpoint manager (for loading on restart)
     */
    CheckpointManager* getCheckpointManager() const;

    // Recording discovery helpers
    int64_t findLatestRecording(const std::string& channel, int32_t streamId);
    int64_t getRecordingStartPosition(int64_t recordingId);
    int64_t getRecordingStopPosition(int64_t recordingId);

private:
    SubscriberConfig config_;

    std::shared_ptr<aeron::Context> context_;
    std::shared_ptr<aeron::Aeron> aeron_;

    std::shared_ptr<aeron::archive::client::Context> archive_context_;
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;

    // ReplayMerge 관련 (Official Aeron ReplayMerge API)
    std::shared_ptr<aeron::Subscription> subscription_;
    std::unique_ptr<aeron::archive::client::ReplayMerge> replay_merge_;

    std::atomic<bool> running_;
    int64_t message_count_;

    // Legacy callback (deprecated)
    MessageCallback message_callback_;

    // Simple gap tracking (온프레미스 최적화)
    int64_t gap_count_;
    int64_t last_message_number_;
    int64_t expected_sequence_;          // 예상 다음 시퀀스 번호

    // Simple duplicate detection (고정 크기 링 버퍼)
    std::vector<int64_t> duplicate_buffer_;  // 고정 크기 중복 체크 버퍼
    size_t duplicate_buffer_pos_;            // 현재 링 버퍼 위치

    // Zero-copy components (required)
    MessageBufferPool* buffer_pool_;     // External buffer pool (not owned)
    MessageBufferQueue* message_queue_;  // External message queue (not owned)

    // Zero-copy statistics
    std::atomic<uint64_t> zc_messages_received_;
    std::atomic<uint64_t> zc_buffer_allocation_failures_;
    std::atomic<uint64_t> zc_queue_full_failures_;

    // Gap recovery statistics
    std::atomic<uint64_t> gaps_detected_;
    std::atomic<uint64_t> gaps_recovered_;
    std::atomic<uint64_t> duplicates_detected_;

    // Checkpoint manager (optional)
    std::unique_ptr<CheckpointManager> checkpoint_;

    void handleMessage(const uint8_t* buffer, size_t length, int64_t position);
    void handleMessageFastPath(const uint8_t* buffer, size_t length, int64_t position);

    // Simple gap recovery (온프레미스 최적화)
    bool checkForGaps(int64_t message_number);
    bool isDuplicate(int64_t message_number);
    void addToDecluplicationBuffer(int64_t message_number);
    bool triggerImmediateGapRecovery(int64_t gap_start, int64_t gap_end);

    // Legacy functions (minimal implementation)
    void printGapStats();
    int64_t extractMessageNumber(const std::string& message);
};

} // namespace example
} // namespace aeron

#endif // AERON_SUBSCRIBER_H
