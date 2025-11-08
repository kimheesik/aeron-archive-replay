#ifndef AERON_SUBSCRIBER_H
#define AERON_SUBSCRIBER_H

#include <memory>
#include <atomic>
#include <string>
#include "Aeron.h"
#include "client/AeronArchive.h"
#include "ReplayToLiveHandler.h"

namespace aeron {
namespace example {

class AeronSubscriber {
public:
    AeronSubscriber();
    ~AeronSubscriber();
    
    bool initialize();
    bool startLive();
    bool startReplay(int64_t startPosition);
    void run();
    void shutdown();

private:
    std::shared_ptr<aeron::Context> context_;
    std::shared_ptr<aeron::Aeron> aeron_;
    
    std::shared_ptr<aeron::archive::client::Context> archive_context_;      // ✅ client 추가
    std::shared_ptr<aeron::archive::client::AeronArchive> archive_;         // ✅ client 추가
    
    std::unique_ptr<ReplayToLiveHandler> replay_to_live_handler_;
    
    std::atomic<bool> running_;
    int64_t message_count_;
    
    void handleMessage(const uint8_t* buffer, size_t length, int64_t position);
};

} // namespace example
} // namespace aeron

#endif // AERON_SUBSCRIBER_H
