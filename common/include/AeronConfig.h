#ifndef AERON_CONFIG_H
#define AERON_CONFIG_H

#include <string>

namespace aeron {
namespace example {

class AeronConfig {
public:
    // Aeron 디렉토리 (각 서버의 로컬)
    static constexpr const char* AERON_DIR = "/dev/shm/aeron";

    // ========================================
    // Archive Control 채널
    // ========================================
    // Publisher 서버 주소 (분산 환경에서는 Publisher 서버 IP로 변경)
    // 예: "aeron:udp?endpoint=192.168.1.10:8010"
    static constexpr const char* ARCHIVE_CONTROL_REQUEST_CHANNEL =
        "aeron:udp?endpoint=localhost:8010";  // TODO: Publisher 서버 IP로 변경
    static constexpr const char* ARCHIVE_CONTROL_RESPONSE_CHANNEL =
        "aeron:udp?endpoint=localhost:0";  // 동적 포트

    // ========================================
    // Publication/Subscription (Multicast)
    // ========================================
    // Multicast 주소: 224.0.1.1 (사설 multicast 범위)
    // 단일 서버 테스트: localhost:40456 사용
    // 분산 환경: 224.0.1.1:40456 사용
    static constexpr const char* PUBLICATION_CHANNEL =
        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr int PUBLICATION_STREAM_ID = 10;

    static constexpr const char* SUBSCRIPTION_CHANNEL =
        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr int SUBSCRIPTION_STREAM_ID = 10;

    // ========================================
    // Replay 채널 (Subscriber 로컬)
    // ========================================
    // Replay는 Archive에서 Subscriber로 유니캐스트 전송
    // 분산 환경에서는 실행 시 --replay-channel 옵션으로 Subscriber IP 지정
    static constexpr const char* REPLAY_CHANNEL =
        "aeron:udp?endpoint=localhost:40457";
    static constexpr int REPLAY_STREAM_ID = 20;

    // ========================================
    // 로컬 테스트 모드 (Localhost)
    // ========================================
    // 단일 서버에서 테스트 시 사용
    static constexpr const char* LOCALHOST_PUBLICATION_CHANNEL =
        "aeron:udp?endpoint=localhost:40456";
    static constexpr const char* LOCALHOST_SUBSCRIPTION_CHANNEL =
        "aeron:udp?endpoint=localhost:40456";

    // 타임아웃
    static constexpr long long IDLE_SLEEP_MS = 1;
    static constexpr long long MESSAGE_TIMEOUT_NS = 10000000000LL; // 10초
};

} // namespace example
} // namespace aeron

#endif // AERON_CONFIG_H
