#include "RecordingController.h"
#include "AeronConfig.h"
#include <iostream>
#include <thread>

namespace aeron {
namespace example {

RecordingController::RecordingController(
    std::shared_ptr<aeron::archive::client::AeronArchive> archive,
    const std::string& channel,
    int streamId)
    : archive_(archive)
    , channel_(channel)
    , stream_id_(streamId)
    , recording_id_(-1)
    , subscription_id_(-1) {
}

RecordingController::~RecordingController() {
    if (isRecording()) {
        stopRecording();
    }
}

bool RecordingController::startRecording() {
    if (isRecording()) {
        std::cerr << "Recording already started" << std::endl;
        return false;
    }

    try {
        std::cout << "Starting recording on channel: " << channel_
                  << ", streamId: " << stream_id_ << std::endl;

        // 먼저 기존 active recording이 있는지 확인
        auto checkExistingRecording = [this](
            std::int64_t controlSessionId,
            std::int64_t correlationId,
            std::int64_t recordingId,
            std::int64_t startTimestamp,
            std::int64_t stopTimestamp,
            std::int64_t startPosition,
            std::int64_t stopPosition,
            std::int32_t initialTermId,
            std::int32_t segmentFileLength,
            std::int32_t termBufferLength,
            std::int32_t mtuLength,
            std::int32_t sessionId,
            std::int32_t streamId,
            const std::string& strippedChannel,
            const std::string& originalChannel,
            const std::string& sourceIdentity) {

            if (streamId == this->stream_id_ && stopTimestamp == 0) {  // Active recording
                this->recording_id_ = recordingId;
                this->subscription_id_ = sessionId;
                std::cout << "Found existing active recording ID: " << recordingId << std::endl;
            }
        };

        // 기존 recording 확인
        std::int32_t existingRecordingCount = archive_->listRecordingsForUri(
            0,
            10,
            channel_,
            stream_id_,
            checkExistingRecording
        );

        if (recording_id_ != -1) {
            // 기존 active recording 발견
            std::cout << "Using existing recording. ID: " << recording_id_ << std::endl;
            return true;
        }

        // 기존 recording이 없으므로 새로 시작
        try {
            subscription_id_ = archive_->startRecording(
                channel_,
                stream_id_,
                aeron::archive::client::AeronArchive::SourceLocation::LOCAL
            );

            std::cout << "Recording subscription created with ID: " << subscription_id_ << std::endl;
        } catch (const std::exception& e) {
            // "recording exists" 에러인 경우, 기존 recording을 다시 찾기
            std::string error_msg = e.what();
            if (error_msg.find("recording exists") != std::string::npos) {
                std::cout << "Recording already exists, searching for existing recording..." << std::endl;

                // 모든 recording (active와 stopped 포함) 검색
                auto findAnyRecording = [this](
                    std::int64_t controlSessionId,
                    std::int64_t correlationId,
                    std::int64_t recordingId,
                    std::int64_t startTimestamp,
                    std::int64_t stopTimestamp,
                    std::int64_t startPosition,
                    std::int64_t stopPosition,
                    std::int32_t initialTermId,
                    std::int32_t segmentFileLength,
                    std::int32_t termBufferLength,
                    std::int32_t mtuLength,
                    std::int32_t sessionId,
                    std::int32_t streamId,
                    const std::string& strippedChannel,
                    const std::string& originalChannel,
                    const std::string& sourceIdentity) {

                    if (streamId == this->stream_id_) {
                        this->recording_id_ = recordingId;
                        std::cout << "Found recording ID: " << recordingId
                                  << (stopTimestamp == 0 ? " (active)" : " (stopped)") << std::endl;
                    }
                };

                archive_->listRecordingsForUri(0, 10, channel_, stream_id_, findAnyRecording);

                if (recording_id_ != -1) {
                    std::cout << "Using existing recording. Messages will be recorded." << std::endl;
                    return true;
                }
            }

            // 다른 에러는 re-throw
            throw;
        }
        
        // Recording ID 얻기 (약간의 대기 필요)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Recording 목록에서 ID 찾기
        auto recordingDescriptorConsumer = [this](
            std::int64_t controlSessionId,
            std::int64_t correlationId,
            std::int64_t recordingId,
            std::int64_t startTimestamp,
            std::int64_t stopTimestamp,
            std::int64_t startPosition,
            std::int64_t stopPosition,
            std::int32_t initialTermId,
            std::int32_t segmentFileLength,
            std::int32_t termBufferLength,
            std::int32_t mtuLength,
            std::int32_t sessionId,
            std::int32_t streamId,
            const std::string& strippedChannel,
            const std::string& originalChannel,
            const std::string& sourceIdentity) {
            
            if (streamId == this->stream_id_ && stopTimestamp == 0) {
                this->recording_id_ = recordingId;
                std::cout << "Found recording ID: " << recordingId << std::endl;
            }
        };
        
        std::int32_t recordingCount = archive_->listRecordingsForUri(
            0,
            10,
            channel_,
            stream_id_,
            recordingDescriptorConsumer
        );
        
        if (recording_id_ == -1) {
            std::cout << "Recording not found yet, waiting and retrying..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // 다시 시도
            recordingCount = archive_->listRecordingsForUri(
                0,
                10,
                channel_,
                stream_id_,
                recordingDescriptorConsumer
            );
            
            if (recording_id_ == -1) {
                std::cerr << "Failed to get recording ID" << std::endl;
                return false;
            }
        }
        
        std::cout << "Recording started successfully. ID: " << recording_id_ << std::endl;
        return true;
        
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to start recording: " << e.what() 
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to start recording: " << e.what() << std::endl;
        return false;
    }
}

bool RecordingController::stopRecording() {
    if (!isRecording()) {
        std::cerr << "No active recording to stop" << std::endl;
        return false;
    }
    
    try {
        std::cout << "Stopping recording ID: " << recording_id_ << std::endl;
        
        // Archive에 recording 중지 요청
        archive_->stopRecording(subscription_id_);
        
        std::cout << "Recording stopped successfully" << std::endl;
        
        recording_id_ = -1;
        subscription_id_ = -1;
        
        return true;
        
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to stop recording: " << e.what() 
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to stop recording: " << e.what() << std::endl;
        return false;
    }
}

} // namespace example
} // namespace aeron
