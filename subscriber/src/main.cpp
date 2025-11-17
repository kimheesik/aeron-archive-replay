#include "AeronSubscriber.h"
#include "ConfigLoader.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <getopt.h>

static aeron::example::AeronSubscriber* g_subscriber = nullptr;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << std::endl;
    if (g_subscriber) {
        g_subscriber->shutdown();
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --config <file>                 Load configuration from INI file\n"
              << "  --aeron-dir <path>              Aeron directory (override config)\n"
              << "  --archive-control <channel>     Archive control channel (override config)\n"
              << "  --replay-merge <recording_id>   Start ReplayMerge from specific recording ID\n"
              << "  --replay-auto                   Auto-discover latest recording and replay\n"
              << "  --position <pos>                Start position for ReplayMerge (default: 0)\n"
              << "  --print-config                  Print current configuration and exit\n"
              << "  -h, --help                      Show this help message\n"
              << "\nNOTE: External MediaDriver (aeronmd) must be running before starting subscriber\n"
              << "\nExamples:\n"
              << "  # Live mode (default)\n"
              << "  " << program_name << " --config config/aeron-local.ini\n"
              << "\n"
              << "  # Auto-discover latest recording and replay from start\n"
              << "  " << program_name << " --config config/aeron-local.ini --replay-auto\n"
              << "\n"
              << "  # Auto-discover with custom start position\n"
              << "  " << program_name << " --config config/aeron-local.ini --replay-auto --position 1000\n"
              << "\n"
              << "  # Manual ReplayMerge with specific recording ID\n"
              << "  " << program_name << " --config config/aeron-local.ini --replay-merge 1\n"
              << "\n"
              << "  # Distributed subscriber connecting to remote Publisher archive\n"
              << "  " << program_name << " --config config/aeron-distributed.ini --archive-control aeron:udp?endpoint=192.168.1.10:8010 --replay-auto\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 1. Config 로딩 (INI 파일 또는 기본값)
    std::string config_file;
    bool print_config_only = false;
    bool replay_merge_mode = false;
    bool replay_auto_mode = false;   // NEW: Auto-discovery mode
    int64_t recording_id = -1;
    int64_t start_position = 0;

    // CLI 옵션으로 override할 값들
    std::string override_aeron_dir;
    std::string override_archive_control;

    // 커맨드라인 옵션 정의
    static struct option long_options[] = {
        {"config",           required_argument, 0, 'f'},
        {"aeron-dir",        required_argument, 0, 'a'},
        {"archive-control",  required_argument, 0, 'c'},
        {"replay-merge",     required_argument, 0, 'r'},
        {"replay-auto",      no_argument,       0, 'R'},  // NEW
        {"position",         required_argument, 0, 'p'},
        {"print-config",     no_argument,       0, 'P'},
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
            case 'r':
                replay_merge_mode = true;
                recording_id = std::stoll(optarg);
                break;
            case 'R':  // NEW: Auto-discovery
                replay_auto_mode = true;
                break;
            case 'p':
                start_position = std::stoll(optarg);
                break;
            case 'P':
                print_config_only = true;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    // Validate options
    if (replay_merge_mode && replay_auto_mode) {
        std::cerr << "Error: Cannot use both --replay-merge and --replay-auto\n" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // 2. Config 로드
    aeron::example::AeronSettings aeron_settings;

    try {
        if (!config_file.empty()) {
            // Config 파일에서 로드
            aeron_settings = aeron::example::ConfigLoader::loadFromFile(config_file);
        } else {
            // 기본값 사용 (AeronConfig.h)
            std::cout << "Using default configuration (AeronConfig.h)" << std::endl;
            aeron_settings = aeron::example::ConfigLoader::loadDefault();
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load configuration: " << e.what() << std::endl;
        return 1;
    }

    // 3. CLI 옵션으로 override
    if (!override_aeron_dir.empty()) {
        aeron_settings.aeron_dir = override_aeron_dir;
        std::cout << "Override: aeron_dir = " << override_aeron_dir << std::endl;
    }
    if (!override_archive_control.empty()) {
        aeron_settings.archive_control_request_channel = override_archive_control;
        std::cout << "Override: archive_control = " << override_archive_control << std::endl;
    }

    // 4. Config 출력 모드
    if (print_config_only) {
        aeron_settings.print();
        return 0;
    }

    // 5. SubscriberConfig에 적용
    aeron::example::SubscriberConfig sub_config;
    sub_config.aeron_dir = aeron_settings.aeron_dir;
    sub_config.archive_control_channel = aeron_settings.archive_control_request_channel;
    sub_config.subscription_channel = aeron_settings.subscription_channel;
    sub_config.subscription_stream_id = aeron_settings.subscription_stream_id;
    sub_config.replay_destination = aeron_settings.replay_channel;

    // 설정 요약 출력
    std::cout << "========================================" << std::endl;
    std::cout << "Subscriber Configuration" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Aeron directory: " << sub_config.aeron_dir << std::endl;
    std::cout << "MediaDriver: External (must be running separately)" << std::endl;
    std::cout << "Archive control: " << sub_config.archive_control_channel << std::endl;
    std::cout << "Subscription channel: " << sub_config.subscription_channel << std::endl;

    // Display mode
    std::string mode_str = "LIVE";
    if (replay_auto_mode) {
        mode_str = "REPLAY_AUTO (auto-discovery)";
    } else if (replay_merge_mode) {
        mode_str = "REPLAY_MERGE";
    }
    std::cout << "Mode: " << mode_str << std::endl;

    if (replay_merge_mode) {
        std::cout << "Recording ID: " << recording_id << std::endl;
        std::cout << "Start position: " << start_position << std::endl;
        std::cout << "Replay destination: " << sub_config.replay_destination << std::endl;
    } else if (replay_auto_mode) {
        std::cout << "Auto-discovery: ENABLED" << std::endl;
        std::cout << "Start position: " << start_position << std::endl;
        std::cout << "Replay destination: " << sub_config.replay_destination << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    // 6. Subscriber 생성 및 실행
    aeron::example::AeronSubscriber subscriber(sub_config);
    g_subscriber = &subscriber;

    if (!subscriber.initialize()) {
        std::cerr << "Failed to initialize subscriber" << std::endl;
        return 1;
    }

    // Start subscriber based on mode
    if (replay_auto_mode) {
        // Auto-discovery mode
        if (!subscriber.startReplayMergeAuto(start_position)) {
            std::cerr << "Failed to start ReplayMerge with auto-discovery" << std::endl;
            return 1;
        }
    } else if (replay_merge_mode) {
        // Manual recording ID mode
        if (!subscriber.startReplayMerge(recording_id, start_position)) {
            std::cerr << "Failed to start ReplayMerge" << std::endl;
            return 1;
        }
    } else {
        // Live mode
        if (!subscriber.startLive()) {
            std::cerr << "Failed to start live" << std::endl;
            return 1;
        }
    }

    subscriber.run();

    return 0;
}
