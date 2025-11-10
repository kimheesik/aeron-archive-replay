#include "AeronPublisher.h"
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
              << "  --aeron-dir <path>           Aeron directory (default: /dev/shm/aeron)\n"
              << "  --pub-channel <channel>      Publication channel (default: aeron:udp?endpoint=localhost:40456)\n"
              << "  --pub-stream-id <id>         Publication stream ID (default: 10)\n"
              << "  --archive-control <channel>  Archive control channel (default: aeron:udp?endpoint=localhost:8010)\n"
              << "  --archive-response <channel> Archive response channel (default: aeron:udp?endpoint=localhost:0)\n"
              << "  --interval <ms>              Message interval in ms (default: 100)\n"
              << "  -h, --help                   Show this help message\n"
              << "\nExample:\n"
              << "  " << program_name << " --pub-channel aeron:udp?endpoint=localhost:40456 --pub-stream-id 10\n"
              << std::endl;
}

int main(int argc, char** argv) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    aeron::example::PublisherConfig config;

    // 커맨드라인 옵션 정의
    static struct option long_options[] = {
        {"aeron-dir",        required_argument, 0, 'a'},
        {"pub-channel",      required_argument, 0, 'c'},
        {"pub-stream-id",    required_argument, 0, 's'},
        {"archive-control",  required_argument, 0, 'r'},
        {"archive-response", required_argument, 0, 'p'},
        {"interval",         required_argument, 0, 'i'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a':
                config.aeron_dir = optarg;
                break;
            case 'c':
                config.publication_channel = optarg;
                break;
            case 's':
                config.publication_stream_id = std::atoi(optarg);
                break;
            case 'r':
                config.archive_control_request_channel = optarg;
                break;
            case 'p':
                config.archive_control_response_channel = optarg;
                break;
            case 'i':
                config.message_interval_ms = std::atoi(optarg);
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    aeron::example::AeronPublisher publisher(config);

    if (!publisher.initialize()) {
        std::cerr << "Failed to initialize publisher" << std::endl;
        return 1;
    }

    publisher.run();

    return 0;
}
