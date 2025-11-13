#ifndef AERON_PUBLISHER_H
#define AERON_PUBLISHER_H

#include <memory>
#include <atomic>
#include <string>
#include "Aeron.h"
#include "client/AeronArchive.h"
#include "RecordingController.h"

namespace aeron {
namespace example {

struct PublisherConfig {
    std::string aeron_dir;
    std::string publication_channel;
    int publication_stream_id;
    std::string archive_control_request_channel;
    std::string archive_control_response_channel;
    int message_interval_ms;
    bool auto_record;  // 자동으로 recording 시작

    PublisherConfig()
        : aeron_dir("/dev/shm/aeron")
        , publication_channel("aeron:udp?endpoint=localhost:40456")
        , publication_stream_id(10)
        , archive_control_request_channel("aeron:udp?endpoint=localhost:8010")
        , archive_control_response_channel("aeron:udp?endpoint=localhost:0")
        , message_interval_ms(100)
        , auto_record(false)  // 기본값: 수동 recording
    {}
};

class AeronPublisher {
public:
    AeronPublisher(const PublisherConfig& config);
    ~AeronPublisher();
    
    bool initialize();
    bool publish(const uint8_t* buffer, size_t length);
    bool startRecording();
    bool stopRecording();
    bool isRecording() const;
    void run();
    void shutdown();

private:
    PublisherConfig config_;

    std::shared_ptr<aeron::Context> context_;
    std::shared_ptr<aeron::Aeron> aeron_;
    std::shared_ptr<aeron::Publication> publication_;

    std::shared_ptr<aeron::archive::client::Context> archive_context_;
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;
    std::unique_ptr<RecordingController> recording_controller_;

    std::atomic<bool> running_;
    int64_t message_count_;
};

} // namespace example
} // namespace aeron

#endif // AERON_PUBLISHER_H
