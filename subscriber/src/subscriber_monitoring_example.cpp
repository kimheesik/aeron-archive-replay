/**
 * Subscriber 모니터링 예제
 *
 * Queue 기반 모니터링 스레드를 사용하여
 * 성능에 영향 없이 메시지 수신 통계를 100건마다 출력합니다.
 *
 * 컴파일:
 *   (subscriber/src/main.cpp에 통합하거나 별도로 사용)
 */

#include "AeronSubscriber.h"
#include "SPSCQueue.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <iomanip>
#include <csignal>

// 전역 종료 플래그
static std::atomic<bool> g_running{true};

// Signal handler
void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main(int argc, char** argv) {
    // Signal handler 등록
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // CLI 인자 파싱
    bool replay_auto = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--replay-auto") {
            replay_auto = true;
        }
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "Subscriber with Monitoring Thread" << std::endl;
    if (replay_auto) {
        std::cout << "Mode: REPLAY_AUTO (Replay → Live)" << std::endl;
    } else {
        std::cout << "Mode: LIVE" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    // ============================================
    // 1. Lock-free Queue 생성
    // ============================================
    MessageStatsQueue stats_queue;

    std::cout << "✓ Lock-free queue created (capacity: "
              << stats_queue.capacity() << " items)" << std::endl;

    // ============================================
    // 2. 모니터링 스레드 생성
    // ============================================
    std::atomic<bool> monitoring_running{true};
    std::atomic<int64_t> skipped_count{0};  // Queue overflow 카운터

    std::thread monitor_thread([&]() {
        std::cout << "✓ Monitoring thread started" << std::endl;

        int64_t counter = 0;
        int64_t total_latency_us = 0;
        int64_t min_latency_us = INT64_MAX;
        int64_t max_latency_us = 0;

        MessageStats stats;

        while (monitoring_running.load(std::memory_order_relaxed)) {
            // Queue에서 통계 가져오기 (non-blocking)
            if (stats_queue.dequeue(stats)) {
                counter++;

                // 레이턴시 계산
                double latency = stats.latency_us();
                if (latency > 0) {
                    total_latency_us += static_cast<int64_t>(latency);
                    min_latency_us = std::min(min_latency_us,
                                              static_cast<int64_t>(latency));
                    max_latency_us = std::max(max_latency_us,
                                              static_cast<int64_t>(latency));
                }

                // 100건마다 통계 출력
                if (counter % 100 == 0) {
                    double avg_latency = counter > 0 ?
                        static_cast<double>(total_latency_us) / counter : 0.0;

                    std::cout << "\n========================================" << std::endl;
                    std::cout << "📊 모니터링 통계 (최근 100건)" << std::endl;
                    std::cout << "========================================" << std::endl;
                    std::cout << "총 메시지 수:   " << counter << std::endl;
                    std::cout << "최근 메시지:    #" << stats.message_number
                              << " at position " << stats.position << std::endl;

                    if (avg_latency > 0) {
                        std::cout << "평균 레이턴시:  " << std::fixed
                                  << std::setprecision(2) << avg_latency << " μs" << std::endl;
                        std::cout << "최소 레이턴시:  " << min_latency_us << " μs" << std::endl;
                        std::cout << "최대 레이턴시:  " << max_latency_us << " μs" << std::endl;
                    }

                    std::cout << "Queue 크기:     " << stats_queue.size()
                              << " / " << stats_queue.capacity() << std::endl;

                    double usage = (double)stats_queue.size() / stats_queue.capacity() * 100.0;
                    std::cout << "Queue 사용률:   " << std::fixed
                              << std::setprecision(2) << usage << "%" << std::endl;

                    int64_t skipped = skipped_count.load(std::memory_order_relaxed);
                    if (skipped > 0) {
                        std::cout << "⚠️  Queue skip:   " << skipped << " messages" << std::endl;
                    }

                    std::cout << "========================================\n" << std::endl;

                    // 100건마다 통계 리셋 (슬라이딩 윈도우)
                    // 또는 누적 통계를 유지하려면 이 부분 제거
                }
            } else {
                // Queue 비어있으면 잠시 대기
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        std::cout << "✓ Monitoring thread stopped. Total processed: "
                  << counter << std::endl;
    });

    // ============================================
    // 3. AeronSubscriber 생성 및 초기화
    // ============================================
    // 설정: Publisher와 동일한 Aeron 디렉토리 사용
    aeron::example::SubscriberConfig config;
    config.aeron_dir = "/home/hesed/shm/aeron";  // Publisher와 동일
    config.subscription_channel = "aeron:udp?endpoint=localhost:40456";
    config.subscription_stream_id = 10;

    aeron::example::AeronSubscriber subscriber(config);

    if (!subscriber.initialize()) {
        std::cerr << "Failed to initialize subscriber" << std::endl;
        monitoring_running = false;
        monitor_thread.join();
        return 1;
    }

    std::cout << "✓ Subscriber initialized" << std::endl;

    // ============================================
    // 4. 모니터링 콜백 설정
    // ============================================
    subscriber.setMessageCallback([&stats_queue, &skipped_count](
        int64_t msg_num,
        int64_t send_ts,
        int64_t recv_ts,
        int64_t pos) {

        // MessageStats 생성 (스택 할당, 32 bytes)
        MessageStats stats(msg_num, send_ts, recv_ts, pos);

        // Non-blocking enqueue (~50ns)
        if (!stats_queue.enqueue(stats)) {
            // Queue 가득 참 - skip (성능 우선)
            skipped_count.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::cout << "✓ Monitoring callback registered" << std::endl;

    // ============================================
    // 5. Live 또는 ReplayMerge 시작
    // ============================================

    if (replay_auto) {
        // ReplayMerge Auto 모드
        std::cout << "\nStarting ReplayMerge Auto mode..." << std::endl;
        if (!subscriber.startReplayMergeAuto(0)) {
            std::cerr << "Failed to start ReplayMerge (falling back to Live)" << std::endl;
            // Fallback to Live mode
            if (!subscriber.startLive()) {
                std::cerr << "Failed to start live mode" << std::endl;
                monitoring_running = false;
                monitor_thread.join();
                return 1;
            }
        }
    } else {
        // Live 모드
        std::cout << "\nStarting Live mode..." << std::endl;
        if (!subscriber.startLive()) {
            std::cerr << "Failed to start live mode" << std::endl;
            monitoring_running = false;
            monitor_thread.join();
            return 1;
        }
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "Subscriber와 모니터링 스레드 실행 중..." << std::endl;
    std::cout << "Ctrl+C로 종료하세요." << std::endl;
    std::cout << "========================================\n" << std::endl;

    // ============================================
    // 6. 실행 (blocking, but check g_running)
    // ============================================

    // Subscriber를 별도 스레드로 실행
    std::thread subscriber_thread([&]() {
        subscriber.run();
    });

    // 메인 스레드는 종료 신호 대기
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ============================================
    // 7. 정리
    // ============================================
    std::cout << "\nShutting down..." << std::endl;

    subscriber.shutdown();
    subscriber_thread.join();

    monitoring_running = false;
    monitor_thread.join();

    std::cout << "\n========================================" << std::endl;
    std::cout << "Subscriber terminated gracefully" << std::endl;
    std::cout << "========================================\n" << std::endl;

    return 0;
}
