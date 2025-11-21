/**
 * Zero-Copy Subscriber (Main)
 *
 * 고성능 zero-copy 아키텍처:
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
 * Usage:
 *   ./aeron_subscriber
 *   ./aeron_subscriber --replay-auto
 *   ./aeron_subscriber --config config/aeron-local.ini --replay-auto
 */

#include "AeronSubscriber.h"
#include "MessageWorker.h"
#include "BufferPool.h"
#include "MessageQueue.h"
#include "SPSCQueue.h"
#include "ConfigLoader.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <getopt.h>

using namespace aeron::example;

// Global shutdown flag
static std::atomic<bool> g_running{true};

// Signal handler
void signalHandler(int signal) {
    std::cout << "\n\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --config <file>                 Load configuration from INI file\n"
              << "  --aeron-dir <path>              Aeron directory (override config)\n"
              << "  --archive-control <channel>     Archive control channel (override config)\n"
              << "  --replay-auto                   Auto-discover latest recording and replay\n"
              << "  --position <pos>                Start position for ReplayMerge (default: 0)\n"
              << "  --print-config                  Print current configuration and exit\n"
              << "\nGap Recovery Options (온프레미스 최적화):\n"
              << "  --no-gap-recovery               Disable gap recovery (default: enabled)\n"
              << "  --gap-tolerance <N>             Max gaps to recover immediately (default: 5)\n"
              << "  --no-duplicate-check            Disable duplicate checking (default: enabled)\n"
              << "  --duplicate-window <N>          Duplicate check window size (default: 1000)\n"
              << "\n"
              << "  -h, --help                      Show this help message\n"
              << "\nNOTE: External MediaDriver (aeronmd) must be running before starting subscriber\n"
              << "\nExamples:\n"
              << "  # Live mode (default with gap recovery)\n"
              << "  " << program_name << "\n"
              << "\n"
              << "  # Live mode with gap recovery disabled (maximum performance)\n"
              << "  " << program_name << " --no-gap-recovery --no-duplicate-check\n"
              << "\n"
              << "  # Auto-discover latest recording and replay from start\n"
              << "  " << program_name << " --replay-auto\n"
              << "\n"
              << "  # Custom gap recovery settings\n"
              << "  " << program_name << " --gap-tolerance 10 --duplicate-window 2000\n"
              << std::endl;
}

int main(int argc, char** argv) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // CLI argument parsing
    std::string config_file;
    bool print_config_only = false;
    bool replay_auto_mode = false;
    int64_t start_position = 0;
    std::string override_aeron_dir;
    std::string override_archive_control;

    // Gap recovery overrides
    bool gap_recovery_override = false;
    bool gap_recovery_enabled = true;  // default: enabled
    bool duplicate_check_override = false;
    bool duplicate_check_enabled = true;  // default: enabled
    int64_t gap_tolerance_override = -1;
    int64_t duplicate_window_override = -1;

    static struct option long_options[] = {
        {"config",           required_argument, 0, 'f'},
        {"aeron-dir",        required_argument, 0, 'a'},
        {"archive-control",  required_argument, 0, 'c'},
        {"replay-auto",      no_argument,       0, 'R'},
        {"position",         required_argument, 0, 'p'},
        {"print-config",     no_argument,       0, 'P'},
        {"no-gap-recovery",  no_argument,       0, 'G'},
        {"gap-tolerance",    required_argument, 0, 'T'},
        {"no-duplicate-check", no_argument,     0, 'D'},
        {"duplicate-window", required_argument, 0, 'W'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'f':
                config_file = optarg;
                break;
            case 'a':
                override_aeron_dir = optarg;
                break;
            case 'c':
                override_archive_control = optarg;
                break;
            case 'R':
                replay_auto_mode = true;
                break;
            case 'p':
                start_position = std::stoll(optarg);
                break;
            case 'P':
                print_config_only = true;
                break;
            case 'G':
                gap_recovery_override = true;
                gap_recovery_enabled = false;
                break;
            case 'T':
                gap_tolerance_override = std::stoll(optarg);
                break;
            case 'D':
                duplicate_check_override = true;
                duplicate_check_enabled = false;
                break;
            case 'W':
                duplicate_window_override = std::stoll(optarg);
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    // Load configuration
    AeronSettings aeron_settings;
    try {
        if (!config_file.empty()) {
            aeron_settings = ConfigLoader::loadFromFile(config_file);
        } else {
            aeron_settings = ConfigLoader::loadDefault();
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load configuration: " << e.what() << std::endl;
        return 1;
    }

    // Apply CLI overrides
    if (!override_aeron_dir.empty()) {
        aeron_settings.aeron_dir = override_aeron_dir;
    }
    if (!override_archive_control.empty()) {
        aeron_settings.archive_control_request_channel = override_archive_control;
    }

    // Print config mode
    if (print_config_only) {
        aeron_settings.print();
        return 0;
    }

    std::cout << "\n==========================================" << std::endl;
    std::cout << "    ZERO-COPY SUBSCRIBER (Default)" << std::endl;
    std::cout << "==========================================" << std::endl;
    if (replay_auto_mode) {
        std::cout << "Mode: REPLAY_AUTO → Live" << std::endl;
    } else {
        std::cout << "Mode: LIVE" << std::endl;
    }
    std::cout << "Config: " << (config_file.empty() ? "Default" : config_file) << std::endl;
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
                    std::cout << "📊 Zero-Copy Stats (last 100 messages)" << std::endl;
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
                              << " (util: " << std::fixed << std::setprecision(1)
                              << (buffer_pool.utilization() * 100.0) << "%)" << std::endl;

                    std::cout << "Message queue:    " << message_queue.size()
                              << " / " << message_queue.capacity()
                              << " (util: " << std::fixed << std::setprecision(1)
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

    std::cout << "Starting Worker Thread..." << std::endl;
    worker.start();

    // ============================================
    // 6. Create and Initialize Subscriber
    // ============================================
    std::cout << "Initializing Subscriber..." << std::endl;

    SubscriberConfig config;
    config.aeron_dir = aeron_settings.aeron_dir;
    config.archive_control_channel = aeron_settings.archive_control_request_channel;
    config.subscription_channel = aeron_settings.subscription_channel;
    config.subscription_stream_id = aeron_settings.subscription_stream_id;
    config.replay_destination = aeron_settings.replay_channel;

    // Apply gap recovery CLI overrides
    if (gap_recovery_override) {
        config.gap_recovery_enabled = gap_recovery_enabled;
    }
    if (duplicate_check_override) {
        config.duplicate_check_enabled = duplicate_check_enabled;
    }
    if (gap_tolerance_override > 0) {
        config.max_gap_tolerance = gap_tolerance_override;
    }
    if (duplicate_window_override > 0) {
        config.duplicate_window_size = duplicate_window_override;
    }

    // Display gap recovery configuration
    std::cout << "\n--- Gap Recovery Configuration ---" << std::endl;
    std::cout << "Gap Recovery: " << (config.gap_recovery_enabled ? "ENABLED" : "DISABLED");
    if (config.gap_recovery_enabled) {
        std::cout << " (tolerance: " << config.max_gap_tolerance << ")";
    }
    std::cout << std::endl;

    std::cout << "Duplicate Check: " << (config.duplicate_check_enabled ? "ENABLED" : "DISABLED");
    if (config.duplicate_check_enabled) {
        std::cout << " (window: " << config.duplicate_window_size << ")";
    }
    std::cout << std::endl;
    std::cout << "-----------------------------------\n" << std::endl;

    AeronSubscriber subscriber(config);

    if (!subscriber.initialize()) {
        std::cerr << "Failed to initialize subscriber" << std::endl;
        monitoring_running = false;
        monitor_thread.join();
        worker.stop();
        return 1;
    }

    // ============================================
    // 7. Initialize Zero-Copy (REQUIRED)
    // ============================================
    std::cout << "Initializing Zero-Copy..." << std::endl;
    subscriber.initializeZeroCopy(&buffer_pool, &message_queue);

    // ============================================
    // 8. Enable Checkpoint
    // ============================================
    std::string checkpoint_file = config.aeron_dir + "/subscriber.checkpoint";
    subscriber.enableCheckpoint(checkpoint_file, 1);  // Flush every 1 second

    // Load checkpoint for restart (if exists)
    CheckpointManager* checkpoint = subscriber.getCheckpointManager();
    int64_t checkpoint_position = checkpoint ? checkpoint->getLastPosition() : 0;

    if (checkpoint_position > 0) {
        std::cout << "✓ Checkpoint found - resuming from position: " << checkpoint_position << std::endl;
        if (replay_auto_mode) {
            start_position = checkpoint_position;
        }
    }

    // ============================================
    // 9. Start Live or ReplayMerge
    // ============================================
    if (replay_auto_mode) {
        std::cout << "\nStarting ReplayMerge Auto mode..." << std::endl;
        if (!subscriber.startReplayMergeAuto(start_position)) {
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
    std::cout << "  ✓ Zero-Copy Subscriber Running" << std::endl;
    std::cout << "  • Subscriber Thread: Aeron reception" << std::endl;
    std::cout << "  • Worker Thread: Message processing" << std::endl;
    std::cout << "  • Monitoring Thread: Statistics" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "\nPress Ctrl+C to stop...\n" << std::endl;

    // ============================================
    // 10. Run Subscriber in separate thread
    // ============================================
    std::thread subscriber_thread([&]() {
        subscriber.run();
    });

    // ============================================
    // 11. Main thread waits for shutdown signal
    // ============================================
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ============================================
    // 12. Graceful Shutdown
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
    // 13. Print Final Statistics
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
    std::cout << "  ✓ Zero-Copy Subscriber Shutdown Complete" << std::endl;
    std::cout << "==========================================" << std::endl;

    return 0;
}