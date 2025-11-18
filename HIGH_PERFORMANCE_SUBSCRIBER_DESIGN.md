# High-Performance Subscriber Architecture Design

**날짜**: 2025-11-18
**목적**: Zero-copy 기반 고성능 메시지 처리 아키텍처 설계
**요구사항**: Subscriber Thread + Worker Thread 분리, Zero-copy Queue, 모니터링 통합

---

## 1. 아키텍처 개요

### 1.1 현재 아키텍처 (Single Thread)

```
┌─────────────────────────────────────────┐
│        Main Thread (Subscriber)         │
│                                         │
│  Aeron Reception                        │
│       ↓                                 │
│  handleMessage()                        │
│       ↓                                 │
│  Parse + Validate                       │
│       ↓                                 │
│  Latency Measurement                    │
│       ↓                                 │
│  Message Callback (monitoring)          │
│       ↓                                 │
│  [Business Logic would go here]         │
└─────────────────────────────────────────┘

제약사항:
- 모든 처리가 Aeron polling 루프에서 발생
- 무거운 비즈니스 로직 시 메시지 수신 지연
- Aeron의 fragment handler가 blocking되면 성능 저하
```

### 1.2 새로운 아키텍처 (Multi Thread + Zero-copy)

```
┌──────────────────────────┐          ┌──────────────────────────┐
│  Subscriber Thread       │          │  Worker Thread(s)        │
│  (Aeron Reception Only)  │          │  (Message Processing)    │
│                          │          │                          │
│  Aeron::poll()           │          │                          │
│      ↓                   │          │  while(running) {        │
│  handleMessage()         │          │    Buffer* buf =         │
│      ↓                   │          │      queue.dequeue()     │
│  recv_timestamp          │          │                          │
│      ↓                   │          │    Parse message         │
│  Get Buffer from Pool    │          │    Validate              │
│      ↓                   │   Queue  │    Business Logic        │
│  Zero-copy to Buffer ────┼─────────>│    Monitoring            │
│      ↓                   │ (Pointer)│                          │
│  Enqueue(buffer*)        │          │    Return buf to pool    │
│      ↓                   │          │  }                       │
│  Continue polling        │          │                          │
└──────────────────────────┘          └──────────────────────────┘
         ↓                                      ↓
    < 1 μs/msg                              Variable time
    (No blocking)                           (Can be slow)

┌──────────────────────────┐
│  Monitoring Thread       │
│  (Statistics)            │
│                          │
│  Read from Stats Queue   │
│  Print every 100 msgs    │
└──────────────────────────┘
```

**핵심 개선점**:
1. **Subscriber Thread**: Aeron 수신만 담당 → 항상 빠르게 poll
2. **Worker Thread**: 무거운 처리 담당 → Aeron 수신과 독립적
3. **Zero-copy Queue**: Buffer 포인터만 전달 → 데이터 복사 없음
4. **Buffer Pool**: 사전 할당 → 동적 할당 오버헤드 제거

---

## 2. Zero-Copy Buffer Pool 설계

### 2.1 Buffer 구조

```cpp
#pragma pack(push, 1)
struct MessageBuffer {
    // Header (64 bytes, cache-line aligned)
    struct {
        uint8_t  magic[4];           // "SEKR"
        uint16_t version;            // Protocol version
        uint16_t message_type;       // Message type
        uint64_t sequence_number;    // Sequence for dedup

        uint64_t event_time_ns;      // Event timestamp
        uint64_t publish_time_ns;    // Publish timestamp
        uint64_t recv_time_ns;       // Receive timestamp (filled by subscriber)

        uint32_t message_length;     // Total length
        uint16_t publisher_id;       // Publisher ID
        uint8_t  priority;           // Priority
        uint8_t  flags;              // Flags
        uint64_t session_id;         // Session ID

        uint32_t checksum;           // CRC32
        uint32_t reserved;           // Future use
    } header;

    // Payload (variable, up to MAX_PAYLOAD_SIZE)
    uint8_t payload[MAX_PAYLOAD_SIZE];  // e.g., 4KB

    // Metadata for pool management (not part of wire format)
    std::atomic<bool> in_use{false};
    uint32_t actual_payload_length;
};
#pragma pack(pop)

// Recommended sizes:
// - MAX_PAYLOAD_SIZE: 4KB (fits most messages)
// - Total buffer: ~4.1KB per buffer
// - Pool size: 1024 buffers = ~4.2MB memory
```

### 2.2 Lock-free Buffer Pool

```cpp
/**
 * Lock-free Buffer Pool
 * - Pre-allocated buffers (avoid malloc/free)
 * - Lock-free allocation/deallocation
 * - O(1) operations
 */
template<size_t PoolSize>
class BufferPool {
public:
    BufferPool() {
        // Initialize free list
        for (size_t i = 0; i < PoolSize; i++) {
            free_list_[i] = &buffers_[i];
        }
        free_count_.store(PoolSize, std::memory_order_release);
    }

    // Allocate buffer (~50-100ns)
    MessageBuffer* allocate() noexcept {
        size_t count = free_count_.load(std::memory_order_acquire);

        while (count > 0) {
            if (free_count_.compare_exchange_weak(
                    count, count - 1,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {

                MessageBuffer* buf = free_list_[count - 1];
                buf->in_use.store(true, std::memory_order_release);
                return buf;
            }
        }

        return nullptr;  // Pool exhausted
    }

    // Deallocate buffer (~50-100ns)
    void deallocate(MessageBuffer* buf) noexcept {
        buf->in_use.store(false, std::memory_order_release);

        size_t count = free_count_.load(std::memory_order_acquire);
        while (count < PoolSize) {
            if (free_count_.compare_exchange_weak(
                    count, count + 1,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {

                free_list_[count] = buf;
                return;
            }
        }
    }

    size_t available() const noexcept {
        return free_count_.load(std::memory_order_acquire);
    }

private:
    alignas(64) MessageBuffer buffers_[PoolSize];
    alignas(64) MessageBuffer* free_list_[PoolSize];
    alignas(64) std::atomic<size_t> free_count_;
};

using MessageBufferPool = BufferPool<1024>;  // 1024 buffers = ~4.2MB
```

**성능 특성**:
- **Allocate**: ~50-100ns (CAS loop)
- **Deallocate**: ~50-100ns (CAS loop)
- **Memory**: 4.2MB (1024 buffers × 4.1KB)
- **Pool exhaustion**: Return nullptr, caller handles backpressure

### 2.3 Zero-Copy Queue

```cpp
/**
 * Zero-Copy Message Queue
 * - Passes buffer pointers, not data
 * - Lock-free SPSC (Single Producer Single Consumer)
 * - Power-of-2 size for fast modulo
 */
template<size_t Size>
class MessageQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");

public:
    // Enqueue buffer pointer (~50ns)
    bool enqueue(MessageBuffer* buf) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Size - 1);

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }

        buffer_[current_tail] = buf;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Dequeue buffer pointer (~50ns)
    bool dequeue(MessageBuffer*& buf) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        buf = buffer_[current_head];
        head_.store((current_head + 1) & (Size - 1), std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h) & (Size - 1);
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) MessageBuffer* buffer_[Size];
};

using MessageBufferQueue = MessageQueue<4096>;  // 4K slots
```

**성능 특성**:
- **Enqueue/Dequeue**: ~50ns (포인터 복사만)
- **Memory**: 32KB (4096 slots × 8 bytes)
- **Capacity**: 4095 messages in-flight
- **Zero-copy**: 데이터 복사 없음, 포인터만 전달

---

## 3. Thread 설계

### 3.1 Subscriber Thread (High Priority)

```cpp
class AeronSubscriber {
public:
    void runSubscriberThread() {
        // Set high priority
        setThreadPriority(THREAD_PRIORITY_HIGH);

        // Optional: Pin to specific CPU core
        // setThreadAffinity(0);  // Core 0

        while (running_.load(std::memory_order_acquire)) {
            // Poll Aeron (non-blocking)
            int fragments = subscription_->poll(
                [this](auto& buffer, auto offset, auto length, auto& header) {
                    this->handleMessageFastPath(buffer, offset, length, header);
                },
                FRAGMENT_LIMIT  // e.g., 10
            );

            if (fragments == 0) {
                // No messages, yield CPU briefly
                std::this_thread::yield();
            }
        }
    }

private:
    void handleMessageFastPath(
        AtomicBuffer& buffer,
        util::index_t offset,
        util::index_t length,
        Header& header) {

        // 1. Record receive timestamp IMMEDIATELY
        int64_t recv_timestamp = getCurrentTimeNanos();

        // 2. Get buffer from pool (~100ns)
        MessageBuffer* msg_buf = buffer_pool_.allocate();

        if (!msg_buf) {
            // Pool exhausted - drop message or handle backpressure
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // 3. Zero-copy: memcpy Aeron buffer to our buffer (~500ns for 4KB)
        //    This is unavoidable - Aeron's buffer is ephemeral
        std::memcpy(&msg_buf->header, buffer.buffer() + offset,
                    std::min(length, sizeof(MessageBuffer)));

        msg_buf->actual_payload_length = length - sizeof(msg_buf->header);
        msg_buf->header.recv_time_ns = recv_timestamp;

        // 4. Enqueue to worker thread (~50ns)
        if (!message_queue_.enqueue(msg_buf)) {
            // Queue full - return buffer to pool
            buffer_pool_.deallocate(msg_buf);
            queue_full_count_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Total time: ~100 + 500 + 50 = ~650ns
        // Compare to current: ~337ns baseline + variable processing
        // Overhead: ~300ns, but enables non-blocking reception
    }

    MessageBufferPool buffer_pool_;
    MessageBufferQueue message_queue_;
    std::atomic<uint64_t> dropped_count_{0};
    std::atomic<uint64_t> queue_full_count_{0};
};
```

**책임**:
- Aeron 메시지 수신만 담당
- 최소한의 처리 (타임스탬프 + 버퍼 복사 + enqueue)
- 항상 빠르게 완료 (~650ns 목표)
- Blocking 작업 절대 금지

**성능 목표**:
- Latency per message: < 1 μs
- Throughput: > 1M msg/s (이론적)

### 3.2 Worker Thread (Normal Priority)

```cpp
class MessageWorker {
public:
    void runWorkerThread(
        MessageBufferQueue& queue,
        MessageBufferPool& pool,
        MessageStatsQueue& stats_queue) {

        MessageBuffer* msg_buf = nullptr;
        uint64_t processed_count = 0;

        // Duplicate detection
        std::unordered_set<uint64_t> seen_sequences;
        seen_sequences.reserve(100000);  // Pre-allocate

        while (running_.load(std::memory_order_acquire)) {
            // 1. Dequeue message (~50ns)
            if (!queue.dequeue(msg_buf)) {
                // Queue empty - adaptive wait
                if (++empty_count_ < 100) {
                    std::this_thread::yield();
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
                continue;
            }

            empty_count_ = 0;

            // 2. Validate message (~200ns)
            if (!validateMessage(msg_buf)) {
                pool.deallocate(msg_buf);
                continue;
            }

            // 3. Duplicate detection (~50ns with hash table)
            uint64_t seq = msg_buf->header.sequence_number;
            if (seen_sequences.count(seq) > 0) {
                // Duplicate detected
                duplicate_count_++;
                pool.deallocate(msg_buf);
                continue;
            }
            seen_sequences.insert(seq);

            // 4. Process message (variable time)
            processMessage(msg_buf);

            // 5. Send to monitoring (~50ns)
            sendToMonitoring(msg_buf, stats_queue);

            // 6. Return buffer to pool (~100ns)
            pool.deallocate(msg_buf);

            processed_count++;
        }
    }

private:
    bool validateMessage(MessageBuffer* buf) {
        // Check magic
        if (memcmp(buf->header.magic, "SEKR", 4) != 0) {
            return false;
        }

        // Verify checksum (if enabled)
        if (buf->header.flags & FLAG_CHECKSUM_ENABLED) {
            uint32_t calculated_crc = calculateCRC32(buf);
            if (calculated_crc != buf->header.checksum) {
                return false;
            }
        }

        return true;
    }

    void processMessage(MessageBuffer* buf) {
        switch (buf->header.message_type) {
            case MSG_ORDER_NEW:
                handleOrderNew(buf);
                break;
            case MSG_ORDER_EXECUTION:
                handleOrderExecution(buf);
                break;
            case MSG_ORDER_MODIFY:
                handleOrderModify(buf);
                break;
            case MSG_ORDER_CANCEL:
                handleOrderCancel(buf);
                break;
            case MSG_QUOTE_UPDATE:
                handleQuoteUpdate(buf);
                break;
            default:
                // Unknown message type
                break;
        }
    }

    void sendToMonitoring(MessageBuffer* buf, MessageStatsQueue& stats_queue) {
        MessageStats stats;
        stats.message_number = buf->header.sequence_number;
        stats.send_timestamp = buf->header.publish_time_ns;
        stats.recv_timestamp = buf->header.recv_time_ns;
        stats.position = 0;  // Not available in this architecture

        // Non-blocking enqueue
        stats_queue.enqueue(stats);
    }

    uint64_t empty_count_ = 0;
    uint64_t duplicate_count_ = 0;
};
```

**책임**:
- 메시지 검증 (magic, checksum)
- 중복 제거 (sequence number 기반)
- 비즈니스 로직 처리
- 모니터링 데이터 전송
- 버퍼 반환

**성능 특성**:
- 처리 시간: Variable (비즈니스 로직에 따라)
- Subscriber thread와 독립적
- 느려도 Aeron 수신에 영향 없음

### 3.3 Monitoring Thread (Low Priority)

```cpp
// 기존 모니터링 스레드와 동일
// MessageStatsQueue에서 dequeue하여 100건마다 출력
// 변경 없음
```

---

## 4. 모니터링 통합 설계

### 4.1 현재 모니터링 구조

```
Subscriber Thread:
  handleMessage()
    ↓
  message_callback_()  (if registered)
    ↓
  MessageStatsQueue.enqueue()

Monitoring Thread:
  MessageStatsQueue.dequeue()
    ↓
  Print stats every 100 messages
```

**문제점**:
- Subscriber thread에서 직접 stats queue에 enqueue
- 새 아키텍처에서는 Worker thread가 메시지 처리

### 4.2 새로운 모니터링 통합

**옵션 A: Worker Thread에서 모니터링** (권장)

```
Subscriber Thread:          Worker Thread:               Monitoring Thread:
  Aeron::poll()              Process message              Dequeue stats
    ↓                           ↓                            ↓
  handleMessageFastPath()     Validate                    Print every 100
    ↓                           ↓
  MessageQueue.enqueue()      Duplicate check
                                ↓
                              Business logic
                                ↓
                              StatsQueue.enqueue() ←──────── Read from here
```

**장점**:
- 중복 제거 후 통계 (정확한 메시지 수)
- 비즈니스 로직 처리 시간 측정 가능
- Subscriber thread 오버헤드 최소화

**단점**:
- 네트워크 레이턴시 + 큐 대기 시간 포함

**옵션 B: Subscriber Thread에서 모니터링**

```
Subscriber Thread:          Worker Thread:               Monitoring Thread:
  Aeron::poll()              Process message              Dequeue stats
    ↓                           ↓                            ↓
  handleMessageFastPath()     Validate                    Print every 100
    ↓                           ↓
  MessageQueue.enqueue()      Duplicate check
    ↓                           ↓
  StatsQueue.enqueue() ──────────────────────────────────→ Read from here
```

**장점**:
- 순수 네트워크 레이턴시 측정
- 중복 포함 수신 통계

**단점**:
- Subscriber thread 오버헤드 증가 (~50ns)
- 중복 메시지 포함된 통계

**옵션 C: Dual Monitoring**

```
Subscriber Thread:          Worker Thread:               Monitoring Thread 1:
  Aeron::poll()              Process message              Network stats
    ↓                           ↓                            ↓
  handleMessageFastPath()     Validate                    Print network latency
    ↓                           ↓
  MessageQueue.enqueue()      Duplicate check            Monitoring Thread 2:
    ↓                           ↓                            ↓
  NetStatsQueue.enqueue() ──→ Business logic              Processing stats
                                ↓                            ↓
                              ProcStatsQueue.enqueue() ───→ Print processing time
```

**장점**:
- 네트워크 레이턴시와 처리 시간 분리 측정
- 상세한 성능 분석 가능

**단점**:
- 복잡도 증가
- 2개 통계 큐 관리 필요

### 4.3 권장 구조 (옵션 A)

```cpp
// Worker Thread에서 모니터링
void MessageWorker::processMessage(MessageBuffer* buf, MessageStatsQueue& stats_queue) {
    // ... validation, duplicate check ...

    // Business logic
    auto start_processing = getCurrentTimeNanos();
    handleBusinessLogic(buf);
    auto end_processing = getCurrentTimeNanos();

    // Send enhanced stats to monitoring
    MessageStats stats;
    stats.message_number = buf->header.sequence_number;
    stats.send_timestamp = buf->header.publish_time_ns;
    stats.recv_timestamp = buf->header.recv_time_ns;
    stats.processing_time_ns = end_processing - start_processing;

    stats_queue.enqueue(stats);
}
```

**통계 출력 예시**:
```
========================================
📊 모니터링 통계 (최근 100건)
========================================
총 메시지 수:   1000 (중복 제거 후)
최근 메시지:    #6704 (seq)
네트워크 레이턴시:  1195.12 μs (avg)
처리 시간:          50.23 μs (avg)
총 E2E 레이턴시:    1245.35 μs (avg)
중복 메시지:        0
Queue 사용률:       0.5%
========================================
```

---

## 5. 성능 분석

### 5.1 지연시간 분해

```
┌─────────────────────────────────────────────────────────┐
│                   End-to-End Latency                    │
└─────────────────────────────────────────────────────────┘
         │                    │                    │
    ┌────▼────┐         ┌─────▼─────┐       ┌─────▼─────┐
    │ Network │         │  Queuing  │       │Processing │
    │ Latency │         │  Latency  │       │  Latency  │
    └─────────┘         └───────────┘       └───────────┘
      ~1000μs              ~10-100μs           Variable

Network Latency:
  - Publisher send → Subscriber receive
  - Measured: buf->header.recv_time_ns - buf->header.publish_time_ns
  - Typical: ~1000μs (1ms)

Queuing Latency:
  - Subscriber enqueue → Worker dequeue
  - Depends on queue depth and worker processing speed
  - Typical: ~10-100μs (low load)

Processing Latency:
  - Worker dequeue → Business logic complete
  - Depends on business logic complexity
  - Target: < 100μs for simple processing
```

### 5.2 성능 목표

| 구성 요소 | 목표 지연시간 | 목표 처리량 |
|-----------|---------------|-------------|
| Subscriber Thread | < 1 μs/msg | > 1M msg/s |
| Buffer Pool Alloc | < 100 ns | - |
| Queue Enqueue | < 50 ns | - |
| Memcpy (4KB) | < 500 ns | - |
| Worker Validation | < 200 ns | - |
| Worker Duplicate Check | < 50 ns | - |
| Worker Business Logic | < 100 μs | > 10K msg/s |
| Monitoring Enqueue | < 50 ns | - |

### 5.3 메모리 사용량

```
Buffer Pool (1024 buffers × 4KB):  ~4.2 MB
Message Queue (4096 pointers):     ~32 KB
Stats Queue (16384 items):         ~512 KB
Duplicate Set (100K entries):      ~3 MB
Thread Stacks (3 × 8MB):           ~24 MB
──────────────────────────────────────────
Total:                             ~32 MB
```

### 5.4 CPU 사용률 예상

```
Low Load (1K msg/s):
  - Subscriber Thread: ~5% (frequent yield)
  - Worker Thread: ~10%
  - Monitoring Thread: ~1%
  - Total: ~16%

Medium Load (100K msg/s):
  - Subscriber Thread: ~50%
  - Worker Thread: ~80%
  - Monitoring Thread: ~5%
  - Total: ~135% (needs 2 cores)

High Load (1M msg/s):
  - Subscriber Thread: ~100% (1 core pinned)
  - Worker Thread: ~100% (1 core pinned)
  - Monitoring Thread: ~10%
  - Total: ~210% (needs 3 cores)

Recommendation:
  - Pin Subscriber to Core 0 (highest priority)
  - Pin Worker to Core 1
  - Monitoring on Core 2 or shared
```

---

## 6. 백프레셔(Backpressure) 처리

### 6.1 시나리오별 대응

**시나리오 1: Buffer Pool 고갈**

```cpp
MessageBuffer* msg_buf = buffer_pool_.allocate();
if (!msg_buf) {
    // Option A: Drop message (성능 우선)
    dropped_count_.fetch_add(1, std::memory_order_relaxed);
    return;

    // Option B: Busy wait (신뢰성 우선)
    while (!msg_buf) {
        std::this_thread::yield();
        msg_buf = buffer_pool_.allocate();
    }
}
```

**권장**: Option A (Drop) + 모니터링 알림

**시나리오 2: Message Queue 가득 참**

```cpp
if (!message_queue_.enqueue(msg_buf)) {
    // Return buffer to pool
    buffer_pool_.deallocate(msg_buf);
    queue_full_count_.fetch_add(1, std::memory_order_relaxed);

    // Optional: Log warning if frequent
    if (queue_full_count_ % 1000 == 0) {
        LOG_WARN("Message queue full, dropped " << queue_full_count_ << " messages");
    }
}
```

**시나리오 3: Worker Thread 느림**

```
Detection:
  - Queue depth > 50% → Warning
  - Queue depth > 80% → Critical
  - Sustained high depth → Add worker thread

Response:
  - Single worker: Optimize business logic
  - Multiple workers: Add more worker threads (thread pool)
```

### 6.2 모니터링 지표

```cpp
struct SystemMetrics {
    uint64_t messages_received;      // Total Aeron messages
    uint64_t messages_processed;     // Successfully processed
    uint64_t messages_dropped;       // Pool exhausted
    uint64_t messages_queue_full;    // Queue full
    uint64_t messages_duplicates;    // Duplicate detected
    uint64_t messages_invalid;       // Validation failed

    size_t buffer_pool_available;    // Free buffers
    size_t message_queue_depth;      // Current queue size
    size_t stats_queue_depth;        // Stats queue size

    double avg_network_latency_us;
    double avg_processing_latency_us;
    double avg_e2e_latency_us;
};

// Print every 10 seconds
void printSystemMetrics(const SystemMetrics& metrics) {
    std::cout << "=== System Metrics ===" << std::endl;
    std::cout << "Received:    " << metrics.messages_received << std::endl;
    std::cout << "Processed:   " << metrics.messages_processed << std::endl;
    std::cout << "Dropped:     " << metrics.messages_dropped << std::endl;
    std::cout << "Queue Full:  " << metrics.messages_queue_full << std::endl;
    std::cout << "Duplicates:  " << metrics.messages_duplicates << std::endl;
    std::cout << "Invalid:     " << metrics.messages_invalid << std::endl;
    std::cout << "Buffer Pool: " << metrics.buffer_pool_available << " / 1024" << std::endl;
    std::cout << "Msg Queue:   " << metrics.message_queue_depth << " / 4096" << std::endl;
}
```

---

## 7. 구현 계획

### 7.1 Phase 1: 기본 구조 (Buffer Pool + Queue)

**Files to Create**:
1. `subscriber/include/MessageBuffer.h` - Buffer 구조 정의
2. `subscriber/include/BufferPool.h` - Lock-free buffer pool
3. `subscriber/include/MessageQueue.h` - Zero-copy queue (포인터 기반)

**Files to Modify**:
1. `subscriber/include/AeronSubscriber.h` - Add buffer pool, message queue
2. `subscriber/src/AeronSubscriber.cpp` - Implement fast path

**Estimated Effort**: 4-6 hours

### 7.2 Phase 2: Worker Thread

**Files to Create**:
1. `subscriber/include/MessageWorker.h` - Worker thread class
2. `subscriber/src/MessageWorker.cpp` - Worker implementation

**Files to Modify**:
1. `subscriber/src/main.cpp` - Launch worker thread

**Estimated Effort**: 3-4 hours

### 7.3 Phase 3: 모니터링 통합

**Files to Modify**:
1. `subscriber/src/MessageWorker.cpp` - Add monitoring stats
2. `subscriber_monitoring_example.cpp` - Update to new architecture

**Estimated Effort**: 2-3 hours

### 7.4 Phase 4: 테스트 및 최적화

**Tasks**:
1. 성능 테스트 (레이턴시, 처리량)
2. 백프레셔 테스트 (고부하)
3. 메모리 누수 확인 (Valgrind)
4. CPU 프로파일링 (perf)

**Estimated Effort**: 4-6 hours

**Total**: ~15-20 hours

---

## 8. 대안 및 트레이드오프

### 8.1 대안 1: Shared Memory IPC (대신 Queue)

**장점**:
- 진정한 zero-copy (같은 프로세스 내)
- Faster than TCP/UDP

**단점**:
- 같은 프로세스 내에서만 동작
- 현재 구조에서 thread 간 통신이므로 불필요

**결론**: 현재 설계 유지 (in-process queue)

### 8.2 대안 2: Thread Pool (대신 Single Worker)

**장점**:
- 높은 처리량 (병렬 처리)
- 부하 분산

**단점**:
- 메시지 순서 보장 어려움 (중요!)
- 복잡도 증가
- 컨텍스트 스위칭 오버헤드

**결론**: Phase 1에서는 Single Worker, 성능 부족 시 Thread Pool 고려

### 8.3 대안 3: Ring Buffer (대신 Queue)

**장점**:
- LMAX Disruptor 같은 검증된 설계
- 매우 높은 처리량

**단점**:
- 복잡한 구현
- 현재 SPSC Queue로 충분

**결론**: 현재 설계 유지, 필요 시 나중에 고려

---

## 9. 마이그레이션 전략

### 9.1 호환성 유지

```cpp
// Old API (backward compatibility)
void AeronSubscriber::setMessageCallback(MessageCallback callback) {
    // Store callback for compatibility
    legacy_callback_ = std::move(callback);
}

// New API
void AeronSubscriber::setWorkerCallback(WorkerCallback callback) {
    worker_callback_ = std::move(callback);
}
```

### 9.2 점진적 마이그레이션

**Step 1**: 기존 코드 유지하면서 새 구조 추가
**Step 2**: 새 빌드 타겟 추가 (`aeron_subscriber_zerocopy`)
**Step 3**: 테스트 및 검증
**Step 4**: 기존 subscriber를 새 구조로 교체

### 9.3 Rollback 계획

- 기존 `aeron_subscriber` 바이너리 유지
- 새 바이너리는 `aeron_subscriber_v2` 로 별도 생성
- 문제 발생 시 기존 바이너리로 즉시 전환

---

## 10. 요약 및 다음 단계

### 10.1 핵심 설계 결정

1. **Thread 분리**: Subscriber (수신) + Worker (처리) + Monitoring
2. **Zero-copy**: Buffer pool + Pointer queue
3. **Lock-free**: 모든 큐와 풀은 lock-free
4. **모니터링**: Worker thread에서 통합 (옵션 A)
5. **백프레셔**: Drop message + 모니터링 알림

### 10.2 예상 성능 개선

| 지표 | 현재 | 예상 | 개선 |
|------|------|------|------|
| Subscriber 응답성 | ~337ns + processing | < 1μs | 일정 |
| 처리량 (theoretical) | ~100K msg/s | > 1M msg/s | 10x |
| 메시지 손실 (고부하) | 높음 (blocking) | 낮음 (queue) | 개선 |
| 레이턴시 변동성 | 높음 | 낮음 | 개선 |

### 10.3 다음 단계

1. **설계 검토** - 사용자 승인
2. **Phase 1 구현** - Buffer pool + Queue
3. **Phase 2 구현** - Worker thread
4. **Phase 3 구현** - 모니터링 통합
5. **Phase 4 테스트** - 성능 검증

---

**문서 작성 완료** - 구현 승인 대기 중
