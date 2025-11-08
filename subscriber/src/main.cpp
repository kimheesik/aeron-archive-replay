#include "AeronSubscriber.h"
#include <iostream>
#include <csignal>
#include <cstring>

static aeron::example::AeronSubscriber* g_subscriber = nullptr;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << std::endl;
    if (g_subscriber) {
        g_subscriber->shutdown();
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 커맨드 라인 파싱
    bool replay_mode = false;
    int64_t start_position = 0;
    
    if (argc > 1) {
        if (strcmp(argv[1], "--replay") == 0) {
            replay_mode = true;
            if (argc > 2) {
                start_position = std::stoll(argv[2]);
            }
        }
    }
    
    aeron::example::AeronSubscriber subscriber;
    g_subscriber = &subscriber;
    
    if (!subscriber.initialize()) {
        std::cerr << "Failed to initialize subscriber" << std::endl;
        return 1;
    }
    
    if (replay_mode) {
        std::cout << "Starting in REPLAY mode from position: " << start_position << std::endl;
        if (!subscriber.startReplay(start_position)) {
            std::cerr << "Failed to start replay" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Starting in LIVE mode" << std::endl;
        if (!subscriber.startLive()) {
            std::cerr << "Failed to start live" << std::endl;
            return 1;
        }
    }
    
    subscriber.run();
    
    return 0;
}
