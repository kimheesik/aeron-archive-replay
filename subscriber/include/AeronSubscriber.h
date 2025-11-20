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

    SubscriberConfig() = default;
};

class AeronSubscriber {
public:
    /**
     * 메시지 모니터링 콜백 타입
     *
     * 파라미터:
     * - message_number: 메시지 번호 (-1이면 파싱 실패)
     * - send_timestamp: 전송 타임스탬프 (나노초)
     * - recv_timestamp: 수신 타임스탬프 (나노초)
     * - position: Aeron stream position
     *
     * 성능 영향: ~10-20ns (std::function 호출)
     */
    using MessageCallback = std::function<void(
        int64_t message_number,
        int64_t send_timestamp,
        int64_t recv_timestamp,
        int64_t position
    )>;

    AeronSubscriber();
    explicit AeronSubscriber(const SubscriberConfig& config);
    ~AeronSubscriber();

    bool initialize();
    bool startLive();
    bool startReplayMerge(int64_t recordingId, int64_t startPosition);
    bool startReplayMergeAuto(int64_t startPosition = 0);  // Auto-discover latest recording
    void run();
    void shutdown();

    /**
     * 메시지 모니터링 콜백 설정
     *
     * 이 콜백은 각 메시지 수신 시 호출됩니다.
     * 성능에 영향을 주지 않도록 콜백 내부에서는:
     * - Lock-free queue 사용 권장
     * - Blocking 작업 금지
     * - 최소한의 작업만 수행
     *
     * @param callback 메시지 통계를 받을 콜백 함수
     */
    void setMessageCallback(MessageCallback callback);

    /**
     * Zero-copy 모드 활성화
     *
     * 이 모드에서는:
     * - Buffer pool과 message queue 사용
     * - Aeron 메시지를 buffer pool에서 할당한 버퍼로 복사
     * - 버퍼 포인터를 message queue에 enqueue
     * - Worker thread가 dequeue하여 처리
     *
     * @param pool Buffer pool (외부에서 생성)
     * @param queue Message queue (외부에서 생성)
     */
    void enableZeroCopyMode(MessageBufferPool* pool, MessageBufferQueue* queue);

    /**
     * Zero-copy 모드 비활성화
     */
    void disableZeroCopyMode();

    /**
     * Zero-copy 모드 활성화 여부
     */
    bool isZeroCopyModeEnabled() const;

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

    // 메시지 모니터링 콜백
    MessageCallback message_callback_;

    // 레이턴시 통계
    double latency_sum_;
    double latency_min_;
    double latency_max_;
    int64_t latency_count_;

    // Gap detection
    int64_t last_message_number_;
    int64_t gap_count_;
    int64_t total_gaps_;

    // Zero-copy mode (optional)
    MessageBufferPool* buffer_pool_;     // External buffer pool (not owned)
    MessageBufferQueue* message_queue_;  // External message queue (not owned)
    bool zero_copy_enabled_;

    // Zero-copy statistics
    std::atomic<uint64_t> zc_messages_received_;
    std::atomic<uint64_t> zc_buffer_allocation_failures_;
    std::atomic<uint64_t> zc_queue_full_failures_;

    // Checkpoint manager (optional)
    std::unique_ptr<CheckpointManager> checkpoint_;

    void handleMessage(const uint8_t* buffer, size_t length, int64_t position);
    void handleMessageFastPath(const uint8_t* buffer, size_t length, int64_t position);
    void printLatencyStats();
    void printGapStats();
    int64_t extractMessageNumber(const std::string& message);
};

} // namespace example
} // namespace aeron

#endif // AERON_SUBSCRIBER_H
