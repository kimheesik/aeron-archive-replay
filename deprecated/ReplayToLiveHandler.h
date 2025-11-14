#ifndef REPLAY_TO_LIVE_HANDLER_H
#define REPLAY_TO_LIVE_HANDLER_H

#include <memory>
#include <atomic>
#include <functional>
#include "Aeron.h"
#include "client/AeronArchive.h"

namespace aeron {
namespace example {

enum class SubscriptionMode {
    REPLAY,
    TRANSITIONING,
    LIVE
};

class ReplayToLiveHandler {
public:
    using MessageHandler = std::function<void(
        const uint8_t* buffer, 
        size_t length, 
        int64_t position)>;
    
    ReplayToLiveHandler(
        std::shared_ptr<aeron::Aeron> aeron,
        std::shared_ptr<aeron::archive::client::AeronArchive> archive);  // ✅ client 추가
    
    ~ReplayToLiveHandler();
    
    bool startReplay(const std::string& channel, int streamId, int64_t startPosition);
    bool startLive(const std::string& channel, int streamId);
    int poll(MessageHandler handler, int fragmentLimit);
    SubscriptionMode getMode() const { return mode_; }
    void shutdown();

private:
    std::shared_ptr<aeron::Aeron> aeron_;
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;  // ✅ client 추가
    
    std::shared_ptr<aeron::Subscription> replay_subscription_;
    std::shared_ptr<aeron::Subscription> live_subscription_;
    
    std::atomic<SubscriptionMode> mode_;
    
    int64_t replay_session_id_;
    int64_t last_replay_position_;
    int64_t live_start_position_;
    
    bool checkTransitionToLive();
};

} // namespace example
} // namespace aeron

#endif // REPLAY_TO_LIVE_HANDLER_H
