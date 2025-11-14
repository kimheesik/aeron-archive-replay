#include "ReplayToLiveHandler.h"
#include "AeronConfig.h"
#include <iostream>
#include <thread>
#include <limits>

namespace aeron {
namespace example {

ReplayToLiveHandler::ReplayToLiveHandler(
    std::shared_ptr<aeron::Aeron> aeron,
    std::shared_ptr<aeron::archive::client::AeronArchive> archive)
    : aeron_(aeron)
    , archive_(archive)
    , mode_(SubscriptionMode::LIVE)
    , replay_session_id_(-1)
    , last_replay_position_(0)
    , live_start_position_(0) {
}

ReplayToLiveHandler::~ReplayToLiveHandler() {
    shutdown();
}

bool ReplayToLiveHandler::startReplay(
    const std::string& channel,
    int streamId,
    int64_t startPosition) {
    
    try {
        std::cout << "Starting replay from position: " << startPosition << std::endl;
        
        mode_ = SubscriptionMode::REPLAY;
        
        // Recording ID 찾기
        int64_t recording_id = -1;
        int64_t stop_position = aeron::archive::client::NULL_POSITION;
        
        auto recordingDescriptorConsumer = [&](
            std::int64_t controlSessionId,
            std::int64_t correlationId,
            std::int64_t recordingId,
            std::int64_t startTimestamp,
            std::int64_t stopTimestamp,
            std::int64_t startPos,
            std::int64_t stopPos,
            std::int32_t initialTermId,
            std::int32_t segmentFileLength,
            std::int32_t termBufferLength,
            std::int32_t mtuLength,
            std::int32_t sessionId,
            std::int32_t recStreamId,
            const std::string& strippedChannel,
            const std::string& originalChannel,
            const std::string& sourceIdentity) {
            
            if (recStreamId == streamId) {
                recording_id = recordingId;
                stop_position = stopPos;
                std::cout << "Found recording ID: " << recordingId 
                         << ", stopPosition: " << stopPos << std::endl;
            }
        };
        
        // Recording 목록 조회
        std::int32_t recordingCount = archive_->listRecordingsForUri(
            0,
            10,
            channel,
            streamId,
            recordingDescriptorConsumer
        );
        
        if (recording_id == -1) {
            std::cerr << "No recording found for channel: " << channel 
                      << ", streamId: " << streamId << std::endl;
            return false;
        }
        
        // Replay subscription 생성
        std::int64_t replay_sub_id = aeron_->addSubscription(
            AeronConfig::REPLAY_CHANNEL,
            AeronConfig::REPLAY_STREAM_ID
        );
        
        // Subscription이 사용 가능할 때까지 대기
        replay_subscription_ = aeron_->findSubscription(replay_sub_id);
        while (!replay_subscription_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            replay_subscription_ = aeron_->findSubscription(replay_sub_id);
        }
        
        std::cout << "Replay subscription created" << std::endl;
        
        // Replay length 계산
        std::int64_t length = (stop_position == 0 || stop_position == aeron::archive::client::NULL_POSITION)
            ? std::numeric_limits<std::int64_t>::max() 
            : (stop_position - startPosition);
        
        // Replay 시작
        replay_session_id_ = archive_->startReplay(
            recording_id,
            startPosition,
            length,
            AeronConfig::REPLAY_CHANNEL,
            AeronConfig::REPLAY_STREAM_ID
        );
        
        std::cout << "Replay started. Session ID: " << replay_session_id_ << std::endl;
        
        last_replay_position_ = startPosition;
        
        // Live subscription도 미리 생성 (전환 준비)
        std::int64_t live_sub_id = aeron_->addSubscription(channel, streamId);
        
        live_subscription_ = aeron_->findSubscription(live_sub_id);
        while (!live_subscription_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            live_subscription_ = aeron_->findSubscription(live_sub_id);
        }
        
        std::cout << "Live subscription pre-created" << std::endl;
        
        // Live의 현재 위치 기록
        live_start_position_ = stop_position;
        
        return true;
        
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to start replay: " << e.what() 
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to start replay: " << e.what() << std::endl;
        return false;
    }
}

bool ReplayToLiveHandler::startLive(
    const std::string& channel,
    int streamId) {
    
    try {
        std::cout << "Starting live subscription" << std::endl;
        
        mode_ = SubscriptionMode::LIVE;
        
        std::int64_t sub_id = aeron_->addSubscription(channel, streamId);
        
        live_subscription_ = aeron_->findSubscription(sub_id);
        while (!live_subscription_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            live_subscription_ = aeron_->findSubscription(sub_id);
        }
        
        std::cout << "Live subscription started" << std::endl;
        
        return true;
        
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to start live: " << e.what() 
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to start live: " << e.what() << std::endl;
        return false;
    }
}

bool ReplayToLiveHandler::checkTransitionToLive() {
    if (mode_ != SubscriptionMode::REPLAY) {
        return false;
    }
    
    // Replay가 완료되었는지 확인
    // Image가 없으면 replay가 완료된 것으로 간주
    if (replay_subscription_ && replay_subscription_->imageCount() == 0) {
        // 잠시 대기 후 다시 확인
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        if (replay_subscription_->imageCount() == 0) {
            std::cout << "Replay completed. Transitioning to live..." << std::endl;
            
            mode_ = SubscriptionMode::TRANSITIONING;
            
            // Replay subscription 종료
            replay_subscription_.reset();
            
            // 잠시 대기 후 live로 전환
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            mode_ = SubscriptionMode::LIVE;
            std::cout << "Transitioned to live mode" << std::endl;
            
            return true;
        }
    }
    
    return false;
}

int ReplayToLiveHandler::poll(MessageHandler handler, int fragmentLimit) {
    int fragments_read = 0;
    
    if (mode_ == SubscriptionMode::REPLAY && replay_subscription_) {
        auto fragment_handler = [&](
            aeron::concurrent::AtomicBuffer& buffer,
            aeron::util::index_t offset,
            aeron::util::index_t length,
            aeron::Header& header) {
            
            last_replay_position_ = header.position();
            handler(buffer.buffer() + offset, length, last_replay_position_);
            fragments_read++;
        };
        
        fragments_read = replay_subscription_->poll(fragment_handler, fragmentLimit);
        
        // Replay 완료 체크 (메시지가 없을 때만)
        if (fragments_read == 0) {
            checkTransitionToLive();
        }
        
    } else if (mode_ == SubscriptionMode::LIVE && live_subscription_) {
        auto fragment_handler = [&](
            aeron::concurrent::AtomicBuffer& buffer,
            aeron::util::index_t offset,
            aeron::util::index_t length,
            aeron::Header& header) {
            
            handler(buffer.buffer() + offset, length, header.position());
            fragments_read++;
        };
        
        fragments_read = live_subscription_->poll(fragment_handler, fragmentLimit);
    }
    
    return fragments_read;
}

void ReplayToLiveHandler::shutdown() {
    replay_subscription_.reset();
    live_subscription_.reset();
}

} // namespace example
} // namespace aeron

