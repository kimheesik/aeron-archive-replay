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

class AeronPublisher {
public:
    AeronPublisher();
    ~AeronPublisher();
    
    bool initialize();
    bool publish(const uint8_t* buffer, size_t length);
    bool startRecording();
    bool stopRecording();
    bool isRecording() const;
    void run();
    void shutdown();

private:
    std::shared_ptr<aeron::Context> context_;
    std::shared_ptr<aeron::Aeron> aeron_;
    std::shared_ptr<aeron::Publication> publication_;
    
    std::shared_ptr<aeron::archive::client::Context> archive_context_;      // ✅ client 추가
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;         // ✅ client 추가
    std::unique_ptr<RecordingController> recording_controller_;
    
    std::atomic<bool> running_;
    int64_t message_count_;
};

} // namespace example
} // namespace aeron

#endif // AERON_PUBLISHER_H
