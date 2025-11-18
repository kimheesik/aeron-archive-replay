/**
 * Zero-Copy Subscriber Example
 *
 * 고성능 zero-copy 아키텍처 데모:
 * - Subscriber Thread: Aeron 수신만 담당 (< 1μs/msg)
 * - Worker Thread: 메시지 처리 (검증, 중복 제거, 비즈니스 로직)
 * - Monitoring Thread: 통계 출력 (100건마다)
 *
 * 특징:
 * - Buffer Pool: 사전 할당된 버퍼 (malloc/free 제거)
 * - Message Queue: 포인터 전달 (데이터 복사 없음)
 * - Lock-free: 모든 큐와 풀은 lock-free
 * - 3-Thread 구조: 완전 독립적 처리
 *
 * 컴파일:
 *   make aeron_subscriber_zerocopy
 *
 * 실행:
 *   ./subscriber/aeron_subscriber_zerocopy
 *   ./subscriber/aeron_subscriber_zerocopy --replay-auto
 */

#include "AeronSubscriber.h"
#include "MessageWorker.h"
#include "BufferPool.h"
#include "MessageQueue.h"
#include "SPSCQueue.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <iomanip>

using namespace aeron::example;

// Global shutdown flag
static std::atomic<bool> g_running{true};

// Signal handler
void signalHandler(int signal) {
    std::cout << "\n\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

int main(int argc, char** argv) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse CLI arguments
    bool replay_auto = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--replay-auto") {
            replay_auto = true;
        }
    }

    std::cout << "\n==========================================" << std::endl;
    std::cout << "    ZERO-COPY SUBSCRIBER (3 Threads)" << std::endl;
    std::cout << "==========================================" << std::endl;
    if (replay_auto) {
        std::cout << "Mode: REPLAY_AUTO → Live" << std::endl;
    } else {
        std::cout << "Mode: LIVE" << std::endl;
    }
    std::cout << "==========================================\n" << std::endl;

    // ============================================
    // 1. Create Buffer Pool (사전 할당)
    // ============================================
    std::cout << "Creating Buffer Pool..." << std::endl;
    MessageBufferPool buffer_pool;  // 1024 buffers (~4.2 MB)

    // ============================================
    // 2. Create Message Queue (zero-copy)
    // ============================================
    std::cout << "Creating Message Queue..." << std::endl;
    MessageBufferQueue message_queue;  // 4096 slots (~32 KB)

    // ============================================
    // 3. Create Monitoring Queue
    // ============================================
    std::cout << "Creating Monitoring Queue..." << std::endl;
    MessageStatsQueue stats_queue;  // 16384 items (~512 KB)

    // ============================================
    // 4. Create Monitoring Thread
    // ============================================
    std::cout << "Starting Monitoring Thread..." << std::endl;
    std::atomic<bool> monitoring_running{true};
    std::atomic<int64_t> skipped_count{0};

    std::thread monitor_thread([&]() {
        int64_t counter = 0;
        int64_t total_latency_us = 0;
        int64_t min_latency_us = INT64_MAX;
        int64_t max_latency_us = 0;

        MessageStats stats;

        while (monitoring_running.load(std::memory_order_relaxed)) {
            if (stats_queue.dequeue(stats)) {
                counter++;

                // Calculate latency
                double latency = stats.latency_us();
                if (latency > 0) {
                    total_latency_us += static_cast<int64_t>(latency);
                    min_latency_us = std::min(min_latency_us, static_cast<int64_t>(latency));
                    max_latency_us = std::max(max_latency_us, static_cast<int64_t>(latency));
                }

                // Print every 100 messages
                if (counter % 100 == 0) {
                    double avg_latency = counter > 0 ?
                        static_cast<double>(total_latency_us) / counter : 0.0;

                    std::cout << "\n==========================================" << std::endl;
                    std::cout << "📊 Monitoring Stats (last 100 messages)" << std::endl;
                    std::cout << "==========================================" << std::endl;
                    std::cout << "Total messages:   " << counter << std::endl;
                    std::cout << "Latest message:   #" << stats.message_number << std::endl;

                    if (avg_latency > 0) {
                        std::cout << std::fixed << std::setprecision(2);
                        std::cout << "Avg latency:      " << avg_latency << " μs" << std::endl;
                        std::cout << "Min latency:      " << min_latency_us << " μs" << std::endl;
                        std::cout << "Max latency:      " << max_latency_us << " μs" << std::endl;
                    }

                    // Buffer pool and queue stats
                    std::cout << "\nResource Usage:" << std::endl;
                    std::cout << "Buffer pool:      " << buffer_pool.available()
                              << " / " << buffer_pool.capacity()
                              << " (utilization: " << std::fixed << std::setprecision(1)
                              << (buffer_pool.utilization() * 100.0) << "%)" << std::endl;

                    std::cout << "Message queue:    " << message_queue.size()
                              << " / " << message_queue.capacity()
                              << " (utilization: " << std::fixed << std::setprecision(1)
                              << (message_queue.utilization() * 100.0) << "%)" << std::endl;

                    std::cout << "Stats queue:      " << stats_queue.size()
                              << " / " << stats_queue.capacity() << std::endl;

                    int64_t skipped = skipped_count.load(std::memory_order_relaxed);
                    if (skipped > 0) {
                        std::cout << "⚠️  Skipped:        " << skipped << " messages" << std::endl;
                    }

                    std::cout << "==========================================\n" << std::endl;
                }
            } else {
                // Queue empty - wait briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        std::cout << "✓ Monitoring thread stopped (total: " << counter << " messages)" << std::endl;
    });

    // ============================================
    // 5. Create Worker Thread
    // ============================================
    std::cout << "Creating Message Worker..." << std::endl;
    MessageWorker worker(message_queue, buffer_pool, stats_queue);

    // Optional: Set custom message handler
    worker.setMessageHandler([](const MessageBuffer* buf) {
        // Custom business logic here
        // Example: Log specific message types
        if (buf->header.message_type == MSG_ORDER_NEW) {
            // Handle new order
        }
    });

    std::cout << "Starting Worker Thread..." << std::endl;
    worker.start();

    // ============================================
    // 6. Create and Initialize Subscriber
    // ============================================
    std::cout << "Initializing Subscriber..." << std::endl;

    SubscriberConfig config;
    config.aeron_dir = "/home/hesed/shm/aeron";
    config.subscription_channel = "aeron:udp?endpoint=localhost:40456";
    config.subscription_stream_id = 10;

    AeronSubscriber subscriber(config);

    if (!subscriber.initialize()) {
        std::cerr << "Failed to initialize subscriber" << std::endl;
        monitoring_running = false;
        monitor_thread.join();
        worker.stop();
        return 1;
    }

    // ============================================
    // 7. Enable Zero-Copy Mode
    // ============================================
    std::cout << "Enabling Zero-Copy Mode..." << std::endl;
    subscriber.enableZeroCopyMode(&buffer_pool, &message_queue);

    // ============================================
    // 8. Start Live or ReplayMerge
    // ============================================
    if (replay_auto) {
        std::cout << "\nStarting ReplayMerge Auto mode..." << std::endl;
        if (!subscriber.startReplayMergeAuto(0)) {
            std::cerr << "Failed to start ReplayMerge (falling back to Live)" << std::endl;
            if (!subscriber.startLive()) {
                std::cerr << "Failed to start live mode" << std::endl;
                monitoring_running = false;
                monitor_thread.join();
                worker.stop();
                return 1;
            }
        }
    } else {
        std::cout << "\nStarting Live mode..." << std::endl;
        if (!subscriber.startLive()) {
            std::cerr << "Failed to start live mode" << std::endl;
            monitoring_running = false;
            monitor_thread.join();
            worker.stop();
            return 1;
        }
    }

    std::cout << "\n==========================================" << std::endl;
    std::cout << "  ✓ All threads running" << std::endl;
    std::cout << "  • Subscriber Thread: Aeron reception" << std::endl;
    std::cout << "  • Worker Thread: Message processing" << std::endl;
    std::cout << "  • Monitoring Thread: Statistics" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "\nPress Ctrl+C to stop...\n" << std::endl;

    // ============================================
    // 9. Run Subscriber in separate thread
    // ============================================
    std::thread subscriber_thread([&]() {
        subscriber.run();
    });

    // ============================================
    // 10. Main thread waits for shutdown signal
    // ============================================
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ============================================
    // 11. Graceful Shutdown
    // ============================================
    std::cout << "\n===========================================" << std::endl;
    std::cout << "  Shutting down..." << std::endl;
    std::cout << "===========================================" << std::endl;

    // Stop subscriber
    std::cout << "1. Stopping subscriber..." << std::endl;
    subscriber.shutdown();
    subscriber_thread.join();

    // Stop worker
    std::cout << "2. Stopping worker thread..." << std::endl;
    worker.stop();

    // Stop monitoring
    std::cout << "3. Stopping monitoring thread..." << std::endl;
    monitoring_running = false;
    monitor_thread.join();

    // ============================================
    // 12. Print Final Statistics
    // ============================================
    std::cout << "\n==========================================" << std::endl;
    std::cout << "  Final Statistics" << std::endl;
    std::cout << "==========================================" << std::endl;

    // Zero-copy stats
    auto zc_stats = subscriber.getZeroCopyStats();
    std::cout << "\nZero-Copy Subscriber:" << std::endl;
    std::cout << "  Messages received:     " << zc_stats.messages_received << std::endl;
    std::cout << "  Buffer alloc failures: " << zc_stats.buffer_allocation_failures << std::endl;
    std::cout << "  Queue full failures:   " << zc_stats.queue_full_failures << std::endl;

    // Worker stats
    std::cout << "\nWorker Thread:" << std::endl;
    worker.printStatistics();

    // Buffer pool stats
    buffer_pool.printStatistics();

    // Message queue stats
    message_queue.printStatistics();

    std::cout << "\n==========================================" << std::endl;
    std::cout << "  ✓ Shutdown complete" << std::endl;
    std::cout << "==========================================" << std::endl;

    return 0;
}
