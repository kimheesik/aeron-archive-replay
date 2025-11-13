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
              << "  --config <file>              Load configuration from INI file\n"
              << "  --aeron-dir <path>           Aeron directory (override config)\n"
              << "  --embedded                   Use embedded MediaDriver (no external driver needed)\n"
              << "  --archive-control <channel>  Archive control channel (override config)\n"
              << "  --replay <position>          Start in replay mode from position\n"
              << "  --print-config               Print current configuration and exit\n"
              << "  -h, --help                   Show this help message\n"
              << "\nExamples:\n"
              << "  # Use config file with embedded driver\n"
              << "  " << program_name << " --config config/aeron-local.ini --embedded\n"
              << "\n"
              << "  # Distributed setup (config file)\n"
              << "  " << program_name << " --config config/aeron-distributed.ini --embedded\n"
              << "\n"
              << "  # Use default config with embedded driver\n"
              << "  " << program_name << " --embedded\n"
              << "\n"
              << "  # Replay mode from position 0\n"
              << "  " << program_name << " --config config/aeron-local.ini --replay 0\n"
              << "\n"
              << "  # Override archive control channel\n"
              << "  " << program_name << " --embedded --archive-control aeron:udp?endpoint=192.168.1.10:8010\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 1. Config 로딩 (INI 파일 또는 기본값)
    std::string config_file;
    bool print_config_only = false;
    bool replay_mode = false;
    int64_t start_position = 0;

    // CLI 옵션으로 override할 값들
    std::string override_aeron_dir;
    bool use_embedded_driver = false;
    std::string override_archive_control;

    // 커맨드라인 옵션 정의
    static struct option long_options[] = {
        {"config",           required_argument, 0, 'f'},
        {"aeron-dir",        required_argument, 0, 'a'},
        {"embedded",         no_argument,       0, 'e'},
        {"archive-control",  required_argument, 0, 'c'},
        {"replay",           required_argument, 0, 'r'},
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
            case 'e':
                use_embedded_driver = true;
                break;
            case 'c':
                override_archive_control = optarg;
                break;
            case 'r':
                replay_mode = true;
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
    sub_config.use_embedded_driver = use_embedded_driver;
    sub_config.archive_control_channel = aeron_settings.archive_control_request_channel;
    sub_config.subscription_channel = aeron_settings.subscription_channel;
    sub_config.subscription_stream_id = aeron_settings.subscription_stream_id;

    // 설정 요약 출력
    std::cout << "========================================" << std::endl;
    std::cout << "Subscriber Configuration" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Aeron directory: " << sub_config.aeron_dir << std::endl;
    std::cout << "Embedded driver: " << (sub_config.use_embedded_driver ? "YES" : "NO") << std::endl;
    std::cout << "Archive control: " << sub_config.archive_control_channel << std::endl;
    std::cout << "Subscription channel: " << aeron_settings.subscription_channel << std::endl;
    std::cout << "Mode: " << (replay_mode ? "REPLAY" : "LIVE") << std::endl;
    if (replay_mode) {
        std::cout << "Start position: " << start_position << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    // 6. Subscriber 생성 및 실행
    aeron::example::AeronSubscriber subscriber(sub_config);
    g_subscriber = &subscriber;

    if (!subscriber.initialize()) {
        std::cerr << "Failed to initialize subscriber" << std::endl;
        return 1;
    }

    if (replay_mode) {
        if (!subscriber.startReplay(start_position)) {
            std::cerr << "Failed to start replay" << std::endl;
            return 1;
        }
    } else {
        if (!subscriber.startLive()) {
            std::cerr << "Failed to start live" << std::endl;
            return 1;
        }
    }

    subscriber.run();

    return 0;
}
