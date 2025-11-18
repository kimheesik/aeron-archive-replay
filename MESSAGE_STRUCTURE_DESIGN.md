# 증권 주문체결 메시지 구조 설계

**작성일**: 2025-11-18
**대상**: 증권사 실시간 주문체결 시스템
**요구사항**: 중복 제거, 모니터링, 고성능

---

## 📋 목차

1. [요구사항 분석](#요구사항-분석)
2. [메시지 구조 설계](#메시지-구조-설계)
3. [메시지 타입 정의](#메시지-타입-정의)
4. [중복 제거 전략](#중복-제거-전략)
5. [구현 예제](#구현-예제)
6. [성능 고려사항](#성능-고려사항)

---

## 요구사항 분석

### **증권사 실시간 주문체결 시스템 특성**

1. **높은 처리량**
   - 장 중 초당 10,000~100,000 건의 메시지
   - 피크 타임에는 더 높은 부하

2. **실시간성 요구**
   - End-to-end 지연 < 10ms 목표
   - 마이크로초 단위 레이턴시 측정 필요

3. **신뢰성**
   - 메시지 손실 불가
   - 중복 메시지 감지 및 제거 필수
   - Gap detection (누락 감지)

4. **주요 메시지 타입**
   - 주문 접수
   - 주문 체결
   - 주문 정정
   - 주문 취소
   - 호가 정보 (선택)

5. **모니터링 요구사항**
   - 메시지 처리 현황 실시간 모니터링
   - 레이턴시 측정 (발생 → 전송 → 수신)
   - 시스템 간 추적 (Tracing)

---

## 메시지 구조 설계

### **설계 원칙**

1. **고정 길이 헤더** - 빠른 파싱
2. **가변 길이 본문** - 유연성
3. **Zero-copy 가능** - 성능 최적화
4. **Cache-friendly** - 64-byte alignment
5. **Endianness 고려** - Network byte order

### **전체 메시지 구조**

```
┌─────────────────────────────────────────────────┐
│  Message Header (64 bytes, fixed)               │
├─────────────────────────────────────────────────┤
│  Message Body (variable length)                 │
│  - Order Message                                │
│  - Execution Message                            │
│  - Modify Message                               │
│  - Cancel Message                               │
└─────────────────────────────────────────────────┘
```

---

## 메시지 타입 정의

### **1. Message Header (공통, 64 bytes)**

```cpp
#pragma pack(push, 1)
struct MessageHeader {
    // === 메시지 식별 (16 bytes) ===
    uint8_t  magic[4];          // "SEKR" (Securities Korea)
    uint16_t version;           // 프로토콜 버전 (예: 0x0100 = v1.0)
    uint16_t message_type;      // 메시지 타입 (아래 enum 참조)
    uint64_t sequence_number;   // 전역 시퀀스 번호 (중복 제거용)

    // === 타임스탬프 (24 bytes) ===
    uint64_t event_time_ns;     // 이벤트 발생 시각 (nanoseconds since epoch)
    uint64_t publish_time_ns;   // Publisher 전송 시각
    uint64_t recv_time_ns;      // Subscriber 수신 시각 (Subscriber가 채움)

    // === 메시지 메타데이터 (16 bytes) ===
    uint32_t message_length;    // 전체 메시지 길이 (header + body)
    uint16_t publisher_id;      // Publisher 식별자
    uint8_t  priority;          // 우선순위 (0=highest, 255=lowest)
    uint8_t  flags;             // 플래그 (비트 필드)
    uint64_t session_id;        // 세션 ID (Replay 시 필요)

    // === 체크섬 및 예약 (8 bytes) ===
    uint32_t checksum;          // CRC32 체크섬 (본문에 대해)
    uint32_t reserved;          // 향후 확장용
};
#pragma pack(pop)

// 메시지 타입 enum
enum class MessageType : uint16_t {
    ORDER_NEW       = 0x0001,   // 신규 주문
    ORDER_EXECUTION = 0x0002,   // 주문 체결
    ORDER_MODIFY    = 0x0003,   // 주문 정정
    ORDER_CANCEL    = 0x0004,   // 주문 취소
    QUOTE_UPDATE    = 0x0005,   // 호가 업데이트
    HEARTBEAT       = 0x0006,   // Heartbeat (연결 확인)
    SYSTEM_EVENT    = 0x0007,   // 시스템 이벤트
};

// Flags 비트 정의
namespace MessageFlags {
    constexpr uint8_t NONE          = 0x00;
    constexpr uint8_t RETRY         = 0x01;  // 재전송 메시지
    constexpr uint8_t URGENT        = 0x02;  // 긴급 메시지
    constexpr uint8_t LAST_FRAGMENT = 0x04;  // 마지막 조각 (fragmentation용)
    constexpr uint8_t TEST_MESSAGE  = 0x80;  // 테스트 메시지
}
```

### **2. 주문 메시지 (ORDER_NEW)**

```cpp
#pragma pack(push, 1)
struct OrderNewMessage {
    MessageHeader header;

    // === 주문 식별 (40 bytes) ===
    char     order_id[20];          // 주문번호 (예: "20250118-000001")
    char     account_number[20];    // 계좌번호

    // === 종목 정보 (16 bytes) ===
    char     symbol[12];            // 종목코드 (예: "005930      ")
    uint8_t  market_type;           // 시장구분 (1=KOSPI, 2=KOSDAQ, etc)
    uint8_t  reserved1[3];

    // === 주문 정보 (24 bytes) ===
    uint8_t  order_side;            // 주문구분 (1=매수, 2=매도)
    uint8_t  order_type;            // 주문유형 (1=지정가, 2=시장가, etc)
    uint8_t  reserved2[2];
    int32_t  order_price;           // 주문가격 (단위: 원, 시장가는 0)
    int64_t  order_quantity;        // 주문수량
    uint64_t order_time_ns;         // 주문접수시각

    // === 추가 정보 (32 bytes) ===
    char     client_id[16];         // 고객 ID
    uint32_t order_conditions;      // 주문조건 (비트 필드)
    uint32_t reserved3;
    uint64_t parent_order_id;       // 부모 주문 ID (알고리즘 주문용)
};
#pragma pack(pop)

// 주문구분
enum class OrderSide : uint8_t {
    BUY  = 1,
    SELL = 2,
};

// 주문유형
enum class OrderType : uint8_t {
    LIMIT         = 1,  // 지정가
    MARKET        = 2,  // 시장가
    STOP_LIMIT    = 3,  // 정지지정가
    BEST_LIMIT    = 4,  // 최유리지정가
    CONDITION     = 5,  // 조건부지정가
};
```

### **3. 체결 메시지 (ORDER_EXECUTION)**

```cpp
#pragma pack(push, 1)
struct OrderExecutionMessage {
    MessageHeader header;

    // === 체결 식별 (40 bytes) ===
    char     execution_id[20];      // 체결번호
    char     order_id[20];          // 원주문번호

    // === 체결 정보 (48 bytes) ===
    char     symbol[12];            // 종목코드
    uint8_t  order_side;            // 주문구분 (1=매수, 2=매도)
    uint8_t  execution_type;        // 체결유형 (1=전부체결, 2=부분체결)
    uint8_t  reserved1[2];

    int32_t  execution_price;       // 체결가격
    int64_t  execution_quantity;    // 체결수량
    int64_t  remaining_quantity;    // 미체결잔량

    uint64_t execution_time_ns;     // 체결시각
    uint64_t cumulative_quantity;   // 누적체결수량
    int64_t  cumulative_amount;     // 누적체결금액

    // === 추가 정보 (32 bytes) ===
    char     account_number[20];    // 계좌번호
    uint32_t settlement_date;       // 결제일 (YYYYMMDD)
    uint32_t reserved2;
    uint64_t trade_id;              // 거래소 체결번호
};
#pragma pack(pop)

// 체결유형
enum class ExecutionType : uint8_t {
    FULL    = 1,  // 전부체결
    PARTIAL = 2,  // 부분체결
};
```

### **4. 주문 정정 메시지 (ORDER_MODIFY)**

```cpp
#pragma pack(push, 1)
struct OrderModifyMessage {
    MessageHeader header;

    // === 주문 식별 (40 bytes) ===
    char     original_order_id[20]; // 원주문번호
    char     new_order_id[20];      // 정정 후 주문번호

    // === 정정 내용 (32 bytes) ===
    char     symbol[12];            // 종목코드
    uint8_t  modify_type;           // 정정구분 (1=가격, 2=수량, 3=가격+수량)
    uint8_t  reserved1[3];

    int32_t  new_price;             // 정정가격
    int64_t  new_quantity;          // 정정수량
    uint64_t modify_time_ns;        // 정정시각

    // === 추가 정보 (16 bytes) ===
    char     account_number[16];    // 계좌번호
};
#pragma pack(pop)
```

### **5. 주문 취소 메시지 (ORDER_CANCEL)**

```cpp
#pragma pack(push, 1)
struct OrderCancelMessage {
    MessageHeader header;

    // === 주문 식별 (40 bytes) ===
    char     order_id[20];          // 취소할 주문번호
    char     cancel_id[20];         // 취소번호

    // === 취소 정보 (32 bytes) ===
    char     symbol[12];            // 종목코드
    uint8_t  cancel_type;           // 취소구분 (1=전체취소, 2=부분취소)
    uint8_t  cancel_reason;         // 취소사유
    uint8_t  reserved1[2];

    int64_t  cancel_quantity;       // 취소수량
    uint64_t cancel_time_ns;        // 취소시각
    uint64_t remaining_quantity;    // 취소 후 잔량

    // === 추가 정보 (16 bytes) ===
    char     account_number[16];    // 계좌번호
};
#pragma pack(pop)
```

---

## 중복 제거 전략

### **1. Sequence Number 기반 중복 제거**

#### **Publisher 측**
```cpp
class MessagePublisher {
private:
    std::atomic<uint64_t> next_sequence_{1};

public:
    template<typename T>
    void publishMessage(T& message) {
        // Sequence number 할당
        message.header.sequence_number = next_sequence_.fetch_add(1);

        // Timestamp 설정
        message.header.publish_time_ns = getCurrentTimeNanos();

        // Checksum 계산
        message.header.checksum = calculateCRC32(
            reinterpret_cast<uint8_t*>(&message) + sizeof(MessageHeader),
            message.header.message_length - sizeof(MessageHeader)
        );

        // 전송
        publication_->offer(
            reinterpret_cast<uint8_t*>(&message),
            message.header.message_length
        );
    }
};
```

#### **Subscriber 측**
```cpp
class MessageSubscriber {
private:
    uint64_t last_sequence_{0};
    std::unordered_set<uint64_t> seen_sequences_;  // 최근 N개 캐시

    // 중복 윈도우 (예: 최근 10만 개)
    static constexpr size_t DEDUP_WINDOW = 100000;

public:
    bool isDuplicate(uint64_t sequence) {
        // 1. 순차적 증가 체크
        if (sequence <= last_sequence_) {
            // 중복 또는 순서 역전
            if (seen_sequences_.count(sequence) > 0) {
                return true;  // 중복
            }
        }

        // 2. Gap 감지
        if (sequence > last_sequence_ + 1) {
            uint64_t gap_count = sequence - last_sequence_ - 1;
            onGapDetected(last_sequence_ + 1, sequence - 1, gap_count);
        }

        // 3. 중복 윈도우 관리
        seen_sequences_.insert(sequence);
        if (seen_sequences_.size() > DEDUP_WINDOW) {
            // 오래된 항목 제거 (LRU 또는 순차적)
            cleanupOldSequences();
        }

        last_sequence_ = std::max(last_sequence_, sequence);
        return false;
    }

    void onMessage(const MessageHeader* header, const uint8_t* body) {
        // 수신 시각 기록
        uint64_t recv_time = getCurrentTimeNanos();

        // 중복 체크
        if (isDuplicate(header->sequence_number)) {
            onDuplicateMessage(header);
            return;
        }

        // Checksum 검증
        if (!verifyChecksum(header, body)) {
            onChecksumError(header);
            return;
        }

        // 메시지 타입별 처리
        switch (static_cast<MessageType>(header->message_type)) {
            case MessageType::ORDER_NEW:
                handleOrderNew(reinterpret_cast<const OrderNewMessage*>(header));
                break;
            case MessageType::ORDER_EXECUTION:
                handleExecution(reinterpret_cast<const OrderExecutionMessage*>(header));
                break;
            // ... 기타 타입
        }

        // 모니터링 콜백
        if (monitoring_callback_) {
            monitoring_callback_(
                header->sequence_number,
                header->publish_time_ns,
                recv_time,
                aeron_position
            );
        }
    }
};
```

### **2. Session 기반 복구**

```cpp
struct SessionState {
    uint64_t session_id;
    uint64_t last_sequence;
    uint64_t first_sequence;
    std::chrono::steady_clock::time_point start_time;
};

class SessionManager {
private:
    std::map<uint64_t, SessionState> sessions_;

public:
    // Replay 시작 전 세션 상태 복구
    uint64_t getReplayStartSequence(uint64_t session_id) {
        auto it = sessions_.find(session_id);
        if (it != sessions_.end()) {
            return it->second.last_sequence + 1;
        }
        return 0;  // 처음부터
    }

    void updateSession(uint64_t session_id, uint64_t sequence) {
        auto& session = sessions_[session_id];
        session.session_id = session_id;
        session.last_sequence = std::max(session.last_sequence, sequence);
        if (session.first_sequence == 0) {
            session.first_sequence = sequence;
        }
    }
};
```

---

## 구현 예제

### **메시지 헤더 파일**

```cpp
// common/include/TradingMessage.h
#ifndef TRADING_MESSAGE_H
#define TRADING_MESSAGE_H

#include <cstdint>
#include <cstring>

// Magic number
constexpr uint8_t MESSAGE_MAGIC[4] = {'S', 'E', 'K', 'R'};
constexpr uint16_t PROTOCOL_VERSION = 0x0100;  // v1.0

// [위의 모든 구조체 정의를 여기에 포함]

// 유틸리티 함수
namespace TradingMessageUtils {

inline uint64_t getCurrentTimeNanos() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

inline void initializeHeader(MessageHeader& header, MessageType type, size_t body_size) {
    std::memcpy(header.magic, MESSAGE_MAGIC, 4);
    header.version = PROTOCOL_VERSION;
    header.message_type = static_cast<uint16_t>(type);
    header.message_length = sizeof(MessageHeader) + body_size;
    header.event_time_ns = getCurrentTimeNanos();
    header.publish_time_ns = 0;  // Publisher가 채울 예정
    header.recv_time_ns = 0;     // Subscriber가 채울 예정
    header.priority = 0;
    header.flags = MessageFlags::NONE;
}

inline bool verifyMagic(const MessageHeader& header) {
    return std::memcmp(header.magic, MESSAGE_MAGIC, 4) == 0;
}

inline uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    // Simple CRC32 implementation (replace with optimized version)
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

} // namespace TradingMessageUtils

#endif // TRADING_MESSAGE_H
```

### **Publisher 사용 예제**

```cpp
// 주문 메시지 발행
OrderNewMessage order_msg;
TradingMessageUtils::initializeHeader(
    order_msg.header,
    MessageType::ORDER_NEW,
    sizeof(OrderNewMessage) - sizeof(MessageHeader)
);

// 주문 정보 설정
std::strncpy(order_msg.order_id, "20250118-000001", 20);
std::strncpy(order_msg.account_number, "1234567890", 20);
std::strncpy(order_msg.symbol, "005930      ", 12);
order_msg.market_type = 1;  // KOSPI
order_msg.order_side = static_cast<uint8_t>(OrderSide::BUY);
order_msg.order_type = static_cast<uint8_t>(OrderType::LIMIT);
order_msg.order_price = 75000;
order_msg.order_quantity = 10;
order_msg.order_time_ns = TradingMessageUtils::getCurrentTimeNanos();

// 발행
publisher.publishMessage(order_msg);
```

### **Subscriber 사용 예제**

```cpp
// 메시지 수신 핸들러
void handleOrderNew(const OrderNewMessage* msg) {
    const auto& header = msg->header;

    // 레이턴시 계산
    uint64_t e2e_latency = header.recv_time_ns - header.event_time_ns;
    uint64_t network_latency = header.recv_time_ns - header.publish_time_ns;

    std::cout << "주문접수: " << msg->order_id << std::endl;
    std::cout << "  종목: " << std::string(msg->symbol, 12) << std::endl;
    std::cout << "  수량: " << msg->order_quantity << std::endl;
    std::cout << "  가격: " << msg->order_price << std::endl;
    std::cout << "  E2E 레이턴시: " << (e2e_latency / 1000.0) << " μs" << std::endl;

    // 비즈니스 로직 처리
    processOrder(msg);
}
```

---

## 성능 고려사항

### **1. 메모리 레이아웃**

```cpp
// 64-byte aligned 구조체
static_assert(sizeof(MessageHeader) == 64, "Header must be 64 bytes");
static_assert(alignof(MessageHeader) == 1, "No padding for network transmission");

// Cache-line aligned 처리
alignas(64) MessageHeader headers[1000];
```

### **2. Zero-Copy 처리**

```cpp
// Aeron fragment handler with zero-copy
void onFragment(
    AtomicBuffer& buffer,
    util::index_t offset,
    util::index_t length,
    Header& header)
{
    // Zero-copy: 직접 버퍼 포인터 사용
    const MessageHeader* msg_header =
        reinterpret_cast<const MessageHeader*>(
            buffer.buffer() + offset
        );

    // 메시지 타입별 처리 (복사 없이)
    switch (static_cast<MessageType>(msg_header->message_type)) {
        case MessageType::ORDER_NEW:
            handleOrderNew(
                reinterpret_cast<const OrderNewMessage*>(msg_header)
            );
            break;
        // ...
    }
}
```

### **3. 성능 목표**

| 항목 | 목표 | 비고 |
|------|------|------|
| 메시지 파싱 | < 100 ns | Zero-copy |
| Sequence 중복체크 | < 50 ns | Hash lookup |
| Checksum 검증 | < 200 ns | CRC32 |
| 전체 처리 | < 500 ns | Per message |

---

## 모니터링 통합

### **MessageStats 확장**

```cpp
struct TradingMessageStats {
    int64_t sequence_number;
    int64_t event_time_ns;
    int64_t publish_time_ns;
    int64_t recv_time_ns;
    int64_t position;

    uint16_t message_type;
    uint16_t publisher_id;

    // 추가 통계
    bool is_duplicate;
    bool checksum_error;
    uint64_t gap_before;  // 이 메시지 전 gap 크기

    // 레이턴시 계산
    double e2e_latency_us() const {
        return (recv_time_ns - event_time_ns) / 1000.0;
    }

    double network_latency_us() const {
        return (recv_time_ns - publish_time_ns) / 1000.0;
    }

    double processing_time_us() const {
        return (publish_time_ns - event_time_ns) / 1000.0;
    }
};
```

---

**다음 단계: 실제 구현 및 통합**

이 설계를 기반으로 다음을 진행할 수 있습니다:
1. TradingMessage.h 헤더 파일 생성
2. Publisher/Subscriber에 메시지 처리 로직 추가
3. 중복 제거 로직 구현
4. 성능 테스트 및 최적화

