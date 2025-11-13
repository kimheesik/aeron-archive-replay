#include "AeronPublisher.h"
#include "ConfigLoader.h"
#include <iostream>
#include <csignal>
#include <getopt.h>
#include <cstdlib>

static std::atomic<bool> running(true);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << std::endl;
    running = false;
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --config <file>              Load configuration from INI file\n"
              << "  --aeron-dir <path>           Aeron directory (override config)\n"
              << "  --pub-channel <channel>      Publication channel (override config)\n"
              << "  --pub-stream-id <id>         Publication stream ID (override config)\n"
              << "  --archive-control <channel>  Archive control channel (override config)\n"
              << "  --archive-response <channel> Archive response channel (override config)\n"
              << "  --interval <ms>              Message interval in ms (default: 100)\n"
              << "  --auto-record                Automatically start recording on startup\n"
              << "  --print-config               Print current configuration and exit\n"
              << "  -h, --help                   Show this help message\n"
              << "\nExamples:\n"
              << "  # Use config file with auto-recording\n"
              << "  " << program_name << " --config config/aeron-local.ini --auto-record\n"
              << "\n"
              << "  # Use config file and override publication channel\n"
              << "  " << program_name << " --config config/aeron-distributed.ini \\\n"
              << "    --pub-channel aeron:udp?endpoint=224.0.1.2:40456\n"
              << "\n"
              << "  # Use default (AeronConfig.h) without config file\n"
              << "  " << program_name << "\n"
              << std::endl;
}

int main(int argc, char** argv) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 1. Config 로딩 (INI 파일 또는 기본값)
    std::string config_file;
    bool print_config_only = false;

    // CLI 옵션으로 override할 값들
    std::string override_aeron_dir;
    std::string override_pub_channel;
    int override_pub_stream_id = -1;
    std::string override_archive_control;
    std::string override_archive_response;
    int override_interval = -1;
    bool auto_record = false;

    // 커맨드라인 옵션 정의
    static struct option long_options[] = {
        {"config",           required_argument, 0, 'f'},
        {"aeron-dir",        required_argument, 0, 'a'},
        {"pub-channel",      required_argument, 0, 'c'},
        {"pub-stream-id",    required_argument, 0, 's'},
        {"archive-control",  required_argument, 0, 'r'},
        {"archive-response", required_argument, 0, 'p'},
        {"interval",         required_argument, 0, 'i'},
        {"auto-record",      no_argument,       0, 'A'},
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
                override_pub_channel = optarg;
                break;
            case 's':
                override_pub_stream_id = std::atoi(optarg);
                break;
            case 'r':
                override_archive_control = optarg;
                break;
            case 'p':
                override_archive_response = optarg;
                break;
            case 'i':
                override_interval = std::atoi(optarg);
                break;
            case 'A':
                auto_record = true;
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
    if (!override_pub_channel.empty()) {
        aeron_settings.publication_channel = override_pub_channel;
        std::cout << "Override: publication_channel = " << override_pub_channel << std::endl;
    }
    if (override_pub_stream_id != -1) {
        aeron_settings.publication_stream_id = override_pub_stream_id;
        std::cout << "Override: publication_stream_id = " << override_pub_stream_id << std::endl;
    }
    if (!override_archive_control.empty()) {
        aeron_settings.archive_control_request_channel = override_archive_control;
        std::cout << "Override: archive_control = " << override_archive_control << std::endl;
    }
    if (!override_archive_response.empty()) {
        aeron_settings.archive_control_response_channel = override_archive_response;
        std::cout << "Override: archive_response = " << override_archive_response << std::endl;
    }

    // 4. Config 출력 모드
    if (print_config_only) {
        aeron_settings.print();
        return 0;
    }

    // 5. PublisherConfig에 적용
    aeron::example::PublisherConfig pub_config;
    pub_config.aeron_dir = aeron_settings.aeron_dir;
    pub_config.publication_channel = aeron_settings.publication_channel;
    pub_config.publication_stream_id = aeron_settings.publication_stream_id;
    pub_config.archive_control_request_channel = aeron_settings.archive_control_request_channel;
    pub_config.archive_control_response_channel = aeron_settings.archive_control_response_channel;
    pub_config.auto_record = auto_record;

    if (override_interval != -1) {
        pub_config.message_interval_ms = override_interval;
    }

    // 6. Publisher 실행
    aeron::example::AeronPublisher publisher(pub_config);

    if (!publisher.initialize()) {
        std::cerr << "Failed to initialize publisher" << std::endl;
        return 1;
    }

    publisher.run();

    return 0;
}
