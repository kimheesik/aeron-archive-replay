#ifndef AERON_CONFIG_H
#define AERON_CONFIG_H

#include <string>

namespace aeron {
namespace example {

class AeronConfig {
public:
    // Aeron 디렉토리
    static constexpr const char* AERON_DIR = "/dev/shm/aeron";
    
    // Archive Control 채널 (Host1)
    static constexpr const char* ARCHIVE_CONTROL_REQUEST_CHANNEL = 
        "aeron:udp?endpoint=localhost:8010";
    static constexpr const char* ARCHIVE_CONTROL_RESPONSE_CHANNEL = 
        "aeron:udp?endpoint=localhost:0";  // ✅ 동적 포트
    
    // Replication 채널 추가 (사용하지 않지만 설정 일관성 유지)
    //static constexpr const char* ARCHIVE_REPLICATION_CHANNEL = 
    //    "aeron:udp?endpoint=localhost:8012";
    static constexpr const char* ARCHIVE_REPLICATION_CHANNEL = 
        "aeron:udp?endpoint=localhost:0";
    
    // Publication 채널 (멀티캐스트)
    static constexpr const char* PUBLICATION_CHANNEL = 
        "aeron:udp?endpoint=224.0.1.1:40456|interface=172.31.33.179";
//        "aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1";
//        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr int PUBLICATION_STREAM_ID = 10;
    
    // Live Subscription 채널
    static constexpr const char* SUBSCRIPTION_CHANNEL = 
        "aeron:udp?endpoint=224.0.1.1:40456|interface=172.31.33.179";
//        "aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1";
//        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr int SUBSCRIPTION_STREAM_ID = 10;
    
    // Replay 채널
    static constexpr const char* REPLAY_CHANNEL = 
        "aeron:udp?endpoint=localhost:0";
    static constexpr int REPLAY_STREAM_ID = 20;
    
    // 타임아웃
    static constexpr long long IDLE_SLEEP_MS = 1;
    static constexpr long long MESSAGE_TIMEOUT_NS = 10000000000LL; // 10초
};

} // namespace example
} // namespace aeron

#endif // AERON_CONFIG_H
