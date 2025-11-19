#include "AeronPublisher.h"
#include "AeronConfig.h"
#include "MessageBuffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <time.h>

namespace aeron {
namespace example {

AeronPublisher::AeronPublisher(const PublisherConfig& config)
    : config_(config)
    , running_(false)
    , message_count_(0) {
}

AeronPublisher::~AeronPublisher() {
    shutdown();
}

bool AeronPublisher::initialize() {
    try {
        std::cout << "Initializing Publisher..." << std::endl;
        std::cout << "  Aeron dir: " << config_.aeron_dir << std::endl;
        std::cout << "  Publication channel: " << config_.publication_channel << std::endl;
        std::cout << "  Publication stream ID: " << config_.publication_stream_id << std::endl;
        std::cout << "  Archive control: " << config_.archive_control_request_channel << std::endl;

        // Aeron Context 설정
        context_ = std::make_shared<aeron::Context>();
        context_->aeronDir(config_.aeron_dir);

        // Aeron 인스턴스 생성
        aeron_ = aeron::Aeron::connect(*context_);
        std::cout << "Connected to Aeron" << std::endl;

        // Publication 생성
        std::int64_t publication_id = aeron_->addPublication(
            config_.publication_channel,
            config_.publication_stream_id
        );

        std::cout << "Publication added with registration ID: " << publication_id << std::endl;

        // Publication이 사용 가능할 때까지 대기
        publication_ = aeron_->findPublication(publication_id);
        while (!publication_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            publication_ = aeron_->findPublication(publication_id);
        }

        std::cout << "Publication ready: " << config_.publication_channel
                  << ", streamId: " << config_.publication_stream_id << std::endl;

        // Archive Context 설정
        archive_context_ = std::make_shared<aeron::archive::client::Context>();
        archive_context_->aeron(aeron_);
        archive_context_->controlRequestChannel(config_.archive_control_request_channel);
        archive_context_->controlResponseChannel(config_.archive_control_response_channel);

        // Archive 연결
        archive_ = aeron::archive::client::AeronArchive::connect(*archive_context_);
        std::cout << "Connected to Archive" << std::endl;

        // Recording Controller 생성
        recording_controller_ = std::make_unique<RecordingController>(
            archive_,
            config_.publication_channel,
            config_.publication_stream_id
        );

        running_ = true;
        std::cout << "Publisher initialized successfully" << std::endl;

        // Auto-record 옵션이 활성화되어 있으면 자동으로 recording 시작
        if (config_.auto_record) {
            std::cout << "Auto-record enabled. Starting recording..." << std::endl;
            if (startRecording()) {
                std::cout << "Recording started automatically" << std::endl;
            } else {
                std::cerr << "Failed to start auto-recording" << std::endl;
            }
        }

        return true;

    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to initialize Publisher: " << e.what()
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Publisher: " << e.what() << std::endl;
        return false;
    }
}

bool AeronPublisher::publish(const uint8_t* buffer, size_t length) {
    if (!running_ || !publication_) {
        return false;
    }
    
    // AtomicBuffer로 래핑
    aeron::concurrent::AtomicBuffer atomic_buffer(
        const_cast<uint8_t*>(buffer), 
        length
    );
    
    std::int64_t result = publication_->offer(atomic_buffer, 0, length);
    
    if (result > 0) {
        // 성공 - result는 새로운 stream position
        message_count_++;
        return true;
    }
    
    // 에러 처리 (음수 값)
    if (result == aeron::BACK_PRESSURED) {
        // Back pressure - 일시적, 재시도 가능
        return false;
    } else if (result == aeron::NOT_CONNECTED) {
        // Publication이 subscriber에 연결되지 않음
        return false;
    } else if (result == aeron::ADMIN_ACTION) {
        // Admin action
        return false;
    } else if (result == aeron::MAX_POSITION_EXCEEDED) {
        // 최대 position 초과
        std::cerr << "Max position exceeded" << std::endl;
        return false;
    } else {
        // 기타 에러
        if (message_count_ % 1000 == 0) {
            std::cerr << "Offer failed with result: " << result << std::endl;
        }
        return false;
    }
}

bool AeronPublisher::startRecording() {
    if (!recording_controller_) {
        std::cerr << "Recording controller not initialized" << std::endl;
        return false;
    }
    return recording_controller_->startRecording();
}

bool AeronPublisher::stopRecording() {
    if (!recording_controller_) {
        std::cerr << "Recording controller not initialized" << std::endl;
        return false;
    }
    return recording_controller_->stopRecording();
}

bool AeronPublisher::isRecording() const {
    return recording_controller_ && recording_controller_->isRecording();
}

void AeronPublisher::run() {
    std::cout << "Publisher running. Type 'start' to begin recording, "
              << "'stop' to end recording, 'quit' to exit." << std::endl;

    // 메시지 발행 스레드
    std::thread publish_thread([this]() {
        uint64_t sequence_number = 0;
        uint16_t publisher_id = 1;  // Publisher ID (can be configured)

        while (running_) {
            // Create message buffer
            uint8_t buffer[sizeof(MessageHeader) + 256];  // Header + small payload
            MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer);

            // Get timestamps
            int64_t event_time = getCurrentTimeNanos();
            int64_t publish_time = getCurrentTimeNanos();

            // Initialize header
            memset(header, 0, sizeof(MessageHeader));
            header->setMagic();
            header->version = 1;
            header->message_type = MSG_TEST;  // Test message type
            header->sequence_number = sequence_number++;
            header->event_time_ns = event_time;
            header->publish_time_ns = publish_time;
            header->recv_time_ns = 0;  // Will be filled by subscriber
            header->publisher_id = publisher_id;
            header->priority = 128;  // Normal priority
            header->flags = FLAG_NONE;
            header->session_id = 1;
            header->checksum = 0;  // Not using checksum for now
            header->reserved = 0;

            // Create payload (simple test data)
            char* payload = reinterpret_cast<char*>(buffer + sizeof(MessageHeader));
            int payload_length = snprintf(payload, 256,
                "Test message %llu from Publisher",
                (unsigned long long)sequence_number - 1);

            // Set total message length
            header->message_length = sizeof(MessageHeader) + payload_length;

            // Calculate and set CRC32 checksum
            header->flags |= FLAG_CHECKSUM_ENABLED;
            header->checksum = calculateMessageCRC32(
                header,
                reinterpret_cast<const uint8_t*>(payload),
                payload_length
            );

            // Publish the message
            if (publish(buffer, header->message_length)) {
                if (message_count_ % 1000 == 0) {
                    std::cout << "Published " << message_count_ << " messages. "
                              << "Recording: " << (isRecording() ? "ON" : "OFF") << std::endl;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(config_.message_interval_ms));
        }
    });
    
    // 사용자 명령 처리
    std::string command;
    while (running_) {
        std::cin >> command;
        
        if (command == "start") {
            startRecording();
        } else if (command == "stop") {
            stopRecording();
        } else if (command == "quit") {
            running_ = false;
            break;
        } else {
            std::cout << "Unknown command. Use: start, stop, quit" << std::endl;
        }
    }
    
    publish_thread.join();
}

void AeronPublisher::shutdown() {
    std::cout << "Shutting down Publisher..." << std::endl;
    
    running_ = false;
    
    if (recording_controller_ && recording_controller_->isRecording()) {
        recording_controller_->stopRecording();
    }
    
    recording_controller_.reset();
    publication_.reset();
    archive_.reset();
    aeron_.reset();
    
    std::cout << "Publisher shutdown complete. Total messages: " << message_count_ << std::endl;
}

} // namespace example
} // namespace aeron

