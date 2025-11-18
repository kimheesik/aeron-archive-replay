#ifndef SPSC_QUEUE_H
#define SPSC_QUEUE_H

#include <atomic>
#include <cstddef>
#include <new>

/**
 * Lock-free Single Producer Single Consumer (SPSC) Ring Buffer Queue
 *
 * 성능 특성:
 * - Enqueue: ~50ns (lock-free)
 * - Dequeue: ~50ns (lock-free)
 * - Cache-friendly (false sharing 방지)
 *
 * 제약사항:
 * - 단일 Producer, 단일 Consumer만 지원
 * - Fixed size (생성 시 크기 결정)
 */
template<typename T, size_t Size>
class SPSCQueue {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");

public:
    SPSCQueue() : head_(0), tail_(0) {
        // Cache line alignment를 위한 초기화
    }

    ~SPSCQueue() = default;

    // Producer: 메시지를 queue에 추가 (Non-blocking)
    // 반환값: true = 성공, false = queue 가득 참
    bool enqueue(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = increment(current_tail);

        // Queue가 가득 찼는지 확인
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;  // Queue full, skip
        }

        // 데이터 저장
        buffer_[current_tail] = item;

        // Tail 업데이트 (Consumer가 볼 수 있도록 release)
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer: queue에서 메시지 가져오기
    // 반환값: true = 성공 (item에 데이터 저장), false = queue 비어있음
    bool dequeue(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Queue가 비어있는지 확인
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        // 데이터 읽기
        item = buffer_[current_head];

        // Head 업데이트 (Producer가 볼 수 있도록 release)
        head_.store(increment(current_head), std::memory_order_release);
        return true;
    }

    // Queue에 있는 아이템 개수 (근사치)
    size_t size() const noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        if (current_tail >= current_head) {
            return current_tail - current_head;
        } else {
            return Size - current_head + current_tail;
        }
    }

    // Queue가 비어있는지 확인
    bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) ==
               tail_.load(std::memory_order_relaxed);
    }

    // 최대 용량
    constexpr size_t capacity() const noexcept {
        return Size - 1;  // 1개는 full/empty 구분용
    }

private:
    // Index를 순환시킴 (power of 2 최적화)
    size_t increment(size_t idx) const noexcept {
        return (idx + 1) & (Size - 1);
    }

    // Cache line padding으로 false sharing 방지
    alignas(64) std::atomic<size_t> head_;  // Consumer가 읽는 위치
    alignas(64) std::atomic<size_t> tail_;  // Producer가 쓰는 위치
    alignas(64) T buffer_[Size];
};

/**
 * 메시지 통계 구조체
 * Cache line 크기(64바이트) 이하로 유지
 */
struct MessageStats {
    int64_t message_number;   // 메시지 번호
    int64_t send_timestamp;   // 전송 타임스탬프 (ns)
    int64_t recv_timestamp;   // 수신 타임스탬프 (ns)
    int64_t position;         // Aeron position

    // 기본 생성자
    MessageStats()
        : message_number(-1)
        , send_timestamp(0)
        , recv_timestamp(0)
        , position(0) {}

    // 매개변수 생성자
    MessageStats(int64_t msg_num, int64_t send_ts, int64_t recv_ts, int64_t pos)
        : message_number(msg_num)
        , send_timestamp(send_ts)
        , recv_timestamp(recv_ts)
        , position(pos) {}

    // 레이턴시 계산 (마이크로초)
    double latency_us() const {
        if (send_timestamp > 0 && recv_timestamp > send_timestamp) {
            return (recv_timestamp - send_timestamp) / 1000.0;
        }
        return 0.0;
    }
};

// 권장 Queue 크기 (2의 거듭제곱)
using MessageStatsQueue = SPSCQueue<MessageStats, 16384>;  // 16K items

#endif // SPSC_QUEUE_H
