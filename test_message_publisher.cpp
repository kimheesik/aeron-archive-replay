/**
 * Standalone Test Publisher for MessageBuffer Format
 *
 * Simple publisher that sends MessageBuffer-formatted messages
 * without interactive console (suitable for background execution)
 */

#include "Aeron.h"
#include "MessageBuffer.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cstring>

using namespace aeron::example;

static std::atomic<bool> running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

int main(int argc, char** argv) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Parse arguments
    int interval_ms = 100;  // default 100ms
    int message_count = 100;  // default 100 messages

    if (argc > 1) {
        interval_ms = std::atoi(argv[1]);
    }
    if (argc > 2) {
        message_count = std::atoi(argv[2]);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  Test MessageBuffer Publisher" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Interval: " << interval_ms << " ms" << std::endl;
    std::cout << "Count: " << message_count << " messages" << std::endl;
    std::cout << "========================================\n" << std::endl;

    try {
        // Connect to Aeron
        auto context = aeron::Context();
        context.aeronDir("/home/hesed/shm/aeron");

        auto aeron = aeron::Aeron::connect(context);
        std::cout << "Connected to Aeron" << std::endl;

        // Add publication
        const std::string channel = "aeron:udp?endpoint=localhost:40456";
        const std::int32_t streamId = 10;

        std::int64_t pubId = aeron->addPublication(channel, streamId);
        std::cout << "Publication added: " << channel << ", streamId: " << streamId << std::endl;

        // Wait for publication to be ready
        auto publication = aeron->findPublication(pubId);
        while (!publication) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            publication = aeron->findPublication(pubId);
        }
        std::cout << "Publication ready" << std::endl;

        // Publish messages
        uint64_t sequence_number = 0;
        uint16_t publisher_id = 1;
        int sent_count = 0;

        std::cout << "\nStarting to publish messages..." << std::endl;

        while (running && sent_count < message_count) {
            // Create message buffer
            uint8_t buffer[sizeof(MessageHeader) + 256];
            MessageHeader* header = reinterpret_cast<MessageHeader*>(buffer);

            // Get timestamps
            int64_t event_time = getCurrentTimeNanos();
            int64_t publish_time = getCurrentTimeNanos();

            // Initialize header
            memset(header, 0, sizeof(MessageHeader));
            header->setMagic();
            header->version = 1;
            header->message_type = MSG_TEST;
            header->sequence_number = sequence_number++;
            header->event_time_ns = event_time;
            header->publish_time_ns = publish_time;
            header->recv_time_ns = 0;
            header->publisher_id = publisher_id;
            header->priority = 128;
            header->flags = FLAG_NONE;
            header->session_id = 1;
            header->checksum = 0;
            header->reserved = 0;

            // Create payload
            char* payload = reinterpret_cast<char*>(buffer + sizeof(MessageHeader));
            int payload_length = snprintf(payload, 256,
                "Test message %llu from Publisher",
                (unsigned long long)sequence_number - 1);

            // Set total message length
            header->message_length = sizeof(MessageHeader) + payload_length;

            // Calculate and set CRC32 checksum
            header->flags |= FLAG_CHECKSUM_ENABLED;
            header->checksum = calculateMessageCRC32(
                header,
                reinterpret_cast<const uint8_t*>(payload),
                payload_length
            );

            // Publish
            aeron::concurrent::AtomicBuffer atomicBuffer(buffer, header->message_length);
            std::int64_t result = publication->offer(atomicBuffer, 0, header->message_length);

            if (result > 0) {
                sent_count++;
                std::cout << "✓ Sent message #" << sent_count << " (seq: "
                          << sequence_number - 1 << ")" << std::endl;
            } else if (result == aeron::BACK_PRESSURED) {
                std::cout << "Back pressured, retrying..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;  // Don't increment, retry this message
            } else if (result == aeron::NOT_CONNECTED) {
                std::cout << "Not connected, waiting for subscriber..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "Publishing complete!" << std::endl;
        std::cout << "Total sent: " << sent_count << " messages" << std::endl;
        std::cout << "========================================" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
