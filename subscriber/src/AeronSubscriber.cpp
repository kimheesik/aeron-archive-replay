#include "AeronSubscriber.h"
#include "AeronConfig.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace aeron {
namespace example {

AeronSubscriber::AeronSubscriber()
    : running_(false)
    , message_count_(0) {
}

AeronSubscriber::~AeronSubscriber() {
    shutdown();
}

bool AeronSubscriber::initialize() {
    try {
        std::cout << "Initializing Subscriber..." << std::endl;
        
        // Aeron Context 설정
        context_ = std::make_shared<aeron::Context>();
        context_->aeronDir(AeronConfig::AERON_DIR);
        
        // Aeron 인스턴스 생성
        aeron_ = aeron::Aeron::connect(*context_);
        std::cout << "Connected to Aeron" << std::endl;
        
        // Archive Context 설정 (Host1의 Archive에 연결)
        archive_context_ = std::make_shared<aeron::archive::client::Context>();
        archive_context_->aeron(aeron_);
        archive_context_->controlRequestChannel(AeronConfig::ARCHIVE_CONTROL_REQUEST_CHANNEL);
        archive_context_->controlResponseChannel(AeronConfig::ARCHIVE_CONTROL_RESPONSE_CHANNEL);
        
        // Archive 연결
        archive_ = aeron::archive::client::AeronArchive::connect(*archive_context_);
        std::cout << "Connected to Archive" << std::endl;
        
        // ReplayToLive Handler 생성
        replay_to_live_handler_ = std::make_unique<ReplayToLiveHandler>(
            aeron_,
            archive_
        );
        
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
    
    return replay_to_live_handler_->startLive(
        AeronConfig::SUBSCRIPTION_CHANNEL,
        AeronConfig::SUBSCRIPTION_STREAM_ID
    );
}

bool AeronSubscriber::startReplay(int64_t startPosition) {
    std::cout << "Starting in REPLAY mode from position: " << startPosition << std::endl;
    
    return replay_to_live_handler_->startReplay(
        AeronConfig::SUBSCRIPTION_CHANNEL,
        AeronConfig::SUBSCRIPTION_STREAM_ID,
        startPosition
    );
}

void AeronSubscriber::handleMessage(
    const uint8_t* buffer, 
    size_t length, 
    int64_t position) {
    
    message_count_++;
    
    if (message_count_ % 100 == 0) {
        auto mode = replay_to_live_handler_->getMode();
        const char* mode_str = (mode == SubscriptionMode::REPLAY) ? "REPLAY" :
                               (mode == SubscriptionMode::TRANSITIONING) ? "TRANSITIONING" :
                               "LIVE";
        
        std::string message(reinterpret_cast<const char*>(buffer), length);
        std::cout << "[" << mode_str << "] Received message #" << message_count_ 
                  << " at position " << position << ": " << message << std::endl;
    }
}

void AeronSubscriber::run() {
    std::cout << "Subscriber running. Press Ctrl+C to exit." << std::endl;
    
    while (running_) {
        int fragments = replay_to_live_handler_->poll(
            [this](const uint8_t* buffer, size_t length, int64_t position) {
                handleMessage(buffer, length, position);
            },
            10  // fragmentLimit
        );
        
        if (fragments == 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(AeronConfig::IDLE_SLEEP_MS));
        }
    }
}

void AeronSubscriber::shutdown() {
    std::cout << "Shutting down Subscriber..." << std::endl;
    
    running_ = false;
    
    if (replay_to_live_handler_) {
        replay_to_live_handler_->shutdown();
    }
    
    replay_to_live_handler_.reset();
    archive_.reset();
    aeron_.reset();
    
    std::cout << "Subscriber shutdown complete. Total messages: " 
              << message_count_ << std::endl;
}

} // namespace example
} // namespace aeron
