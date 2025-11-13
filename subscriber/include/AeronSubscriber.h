#ifndef AERON_SUBSCRIBER_H
#define AERON_SUBSCRIBER_H

#include <memory>
#include <atomic>
#include <string>
#include <sys/types.h>
#include "Aeron.h"
#include "client/AeronArchive.h"
#include "ReplayToLiveHandler.h"

namespace aeron {
namespace example {

struct SubscriberConfig {
    std::string aeron_dir = "/dev/shm/aeron";
    bool use_embedded_driver = false;  // Embedded MediaDriver 사용 여부
    std::string archive_control_channel = "";  // 비어있으면 AeronConfig 사용
    std::string subscription_channel = "";     // 비어있으면 AeronConfig 사용
    int subscription_stream_id = 10;           // 기본값

    SubscriberConfig() = default;
};

class AeronSubscriber {
public:
    AeronSubscriber();
    explicit AeronSubscriber(const SubscriberConfig& config);
    ~AeronSubscriber();

    bool initialize();
    bool startLive();
    bool startReplay(int64_t startPosition);
    void run();
    void shutdown();

private:
    SubscriberConfig config_;

    std::shared_ptr<aeron::Context> context_;
    std::shared_ptr<aeron::Aeron> aeron_;

    std::shared_ptr<aeron::archive::client::Context> archive_context_;
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;

    std::unique_ptr<ReplayToLiveHandler> replay_to_live_handler_;

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

    void handleMessage(const uint8_t* buffer, size_t length, int64_t position);
    void printLatencyStats();
};

} // namespace example
} // namespace aeron

#endif // AERON_SUBSCRIBER_H
