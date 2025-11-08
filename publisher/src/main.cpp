#include "AeronPublisher.h"
#include <iostream>
#include <csignal>

static std::atomic<bool> running(true);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << std::endl;
    running = false;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    aeron::example::AeronPublisher publisher;
    
    if (!publisher.initialize()) {
        std::cerr << "Failed to initialize publisher" << std::endl;
        return 1;
    }
    
    publisher.run();
    
    return 0;
}
