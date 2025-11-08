#ifndef RECORDING_CONTROLLER_H
#define RECORDING_CONTROLLER_H

#include <memory>
#include <string>
#include "Aeron.h"
#include "client/AeronArchive.h"

namespace aeron {
namespace example {

class RecordingController {
public:
    RecordingController(
        std::shared_ptr<aeron::archive::client::AeronArchive> archive,  // ✅ client 추가
        const std::string& channel,
        int streamId);
    
    ~RecordingController();
    
    bool startRecording();
    bool stopRecording();
    bool isRecording() const { return recording_id_ != -1; }
    int64_t getRecordingId() const { return recording_id_; }

private:
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;  // ✅ client 추가
    std::string channel_;
    int stream_id_;
    int64_t recording_id_;
    int64_t subscription_id_;
};

} // namespace example
} // namespace aeron

#endif // RECORDING_CONTROLLER_H
