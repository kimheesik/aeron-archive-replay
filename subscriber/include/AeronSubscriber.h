#ifndef AERON_SUBSCRIBER_H
#define AERON_SUBSCRIBER_H

#include <memory>
#include <atomic>
#include <string>
#include <sys/types.h>
#include "Aeron.h"
#include "client/AeronArchive.h"

namespace aeron {
namespace example {

struct SubscriberConfig {
    std::string aeron_dir = "/home/hesed/shm/aeron-subscriber";
    bool use_embedded_driver = true;   // Embedded MediaDriver 필수 (항상 true)
    std::string archive_control_channel = "";  // 비어있으면 AeronConfig 사용
    std::string subscription_channel = "";     // 비어있으면 AeronConfig 사용
    int subscription_stream_id = 10;           // 기본값
    std::string replay_destination = "aeron:udp?endpoint=localhost:40457";  // ReplayMerge destination

    SubscriberConfig() = default;
};

class AeronSubscriber {
public:
    AeronSubscriber();
    explicit AeronSubscriber(const SubscriberConfig& config);
    ~AeronSubscriber();

    bool initialize();
    bool startLive();
    bool startReplayMerge(int64_t recordingId, int64_t startPosition);
    bool startReplayMergeAuto(int64_t startPosition = 0);  // Auto-discover latest recording
    void run();
    void shutdown();

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

    // ReplayMerge 관련
    std::shared_ptr<aeron::Subscription> subscription_;
    int64_t replay_merge_session_id_;
    bool is_replay_merge_active_;

    std::atomic<bool> running_;
    int64_t message_count_;

    // Embedded MediaDriver 관련
    pid_t driver_pid_;  // MediaDriver 프로세스 ID
    bool startEmbeddedDriver();
    void stopEmbeddedDriver();
    bool waitForDriverReady();

    // 레이턴시 통계
    double latency_sum_;
    double latency_min_;
    double latency_max_;
    int64_t latency_count_;

    // Gap detection
    int64_t last_message_number_;
    int64_t gap_count_;
    int64_t total_gaps_;

    void handleMessage(const uint8_t* buffer, size_t length, int64_t position);
    void printLatencyStats();
    void printGapStats();
    int64_t extractMessageNumber(const std::string& message);
};

} // namespace example
} // namespace aeron

#endif // AERON_SUBSCRIBER_H
