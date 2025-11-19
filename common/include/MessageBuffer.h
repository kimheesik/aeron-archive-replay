/**
 * MessageBuffer.h
 *
 * Zero-copy message buffer structure for high-performance subscriber
 *
 * Design:
 * - Fixed 64-byte header (cache-line aligned)
 * - Variable payload (up to MAX_PAYLOAD_SIZE)
 * - Pool management metadata
 * - Based on MESSAGE_STRUCTURE_DESIGN.md
 */

#ifndef AERON_EXAMPLE_MESSAGE_BUFFER_H
#define AERON_EXAMPLE_MESSAGE_BUFFER_H

#include <cstdint>
#include <atomic>
#include <cstring>
#include <algorithm>  // for std::min
#include <array>      // for std::array (CRC32 table)
#include <utility>    // for std::index_sequence (CRC32 table generation)
#include <time.h>     // for clock_gettime, timespec

namespace aeron {
namespace example {

// Configuration
constexpr size_t MAX_PAYLOAD_SIZE = 4096;  // 4KB payload max
constexpr uint32_t MESSAGE_MAGIC = 0x5345'4B52;  // "SEKR" in little-endian

// Message types (from MESSAGE_STRUCTURE_DESIGN.md)
enum MessageType : uint16_t {
    MSG_ORDER_NEW = 1,
    MSG_ORDER_EXECUTION = 2,
    MSG_ORDER_MODIFY = 3,
    MSG_ORDER_CANCEL = 4,
    MSG_QUOTE_UPDATE = 5,
    MSG_HEARTBEAT = 6,
    MSG_TEST = 99  // For testing
};

// Message flags
enum MessageFlags : uint8_t {
    FLAG_NONE = 0x00,
    FLAG_CHECKSUM_ENABLED = 0x01,
    FLAG_COMPRESSED = 0x02,
    FLAG_ENCRYPTED = 0x04,
    FLAG_URGENT = 0x08
};

/**
 * CRC32 calculation (IEEE 802.3 polynomial: 0x04C11DB7)
 *
 * Fast table-based implementation for message integrity verification
 */
namespace {
    // CRC32 lookup table (generated once)
    constexpr uint32_t CRC32_POLYNOMIAL = 0xEDB88320;

    constexpr uint32_t generateCRC32Entry(uint8_t index) {
        uint32_t crc = index;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ ((crc & 1) ? CRC32_POLYNOMIAL : 0);
        }
        return crc;
    }

    // Compile-time CRC32 table generation
    template<size_t... I>
    constexpr auto makeCRC32Table(std::index_sequence<I...>) {
        return std::array<uint32_t, 256>{generateCRC32Entry(I)...};
    }

    constexpr auto CRC32_TABLE = makeCRC32Table(std::make_index_sequence<256>{});
}

// Forward declaration
struct MessageHeader;

/**
 * Calculate CRC32 checksum for a data buffer
 */
inline uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; ++i) {
        uint8_t index = (crc ^ data[i]) & 0xFF;
        crc = (crc >> 8) ^ CRC32_TABLE[index];
    }

    return ~crc;
}

/**
 * Calculate CRC32 for MessageBuffer (header + payload)
 *
 * Note: Checksum field itself is excluded from calculation
 */
inline uint32_t calculateMessageCRC32(const MessageHeader* header, const uint8_t* payload, uint32_t payload_length);

/**
 * Message Header (64 bytes, cache-line aligned)
 *
 * Layout designed for fast parsing and zero-copy processing
 */
#pragma pack(push, 1)
struct MessageHeader {
    // Magic + Version (6 bytes)
    uint8_t  magic[4];           // "SEKR" (0x52 0x4B 0x45 0x53)
    uint16_t version;            // Protocol version (current: 1)
    uint16_t message_type;       // MessageType enum

    // Sequence + Deduplication (8 bytes)
    uint64_t sequence_number;    // Monotonic sequence for dedup

    // Timestamps (24 bytes)
    uint64_t event_time_ns;      // Event occurrence time (nanoseconds)
    uint64_t publish_time_ns;    // Publisher send time (nanoseconds)
    uint64_t recv_time_ns;       // Receiver timestamp (filled by subscriber)

    // Message metadata (16 bytes)
    uint32_t message_length;     // Total message length (header + payload)
    uint16_t publisher_id;       // Publisher identifier
    uint8_t  priority;           // Message priority (0-255)
    uint8_t  flags;              // MessageFlags bitfield
    uint64_t session_id;         // Session/connection ID

    // Integrity + Reserved (8 bytes)
    uint32_t checksum;           // CRC32 checksum (if enabled)
    uint32_t reserved;           // Reserved for future use

    // Total: 64 bytes

    // Helper methods
    bool isValid() const {
        return memcmp(magic, "SEKR", 4) == 0;
    }

    void setMagic() {
        magic[0] = 'S';
        magic[1] = 'E';
        magic[2] = 'K';
        magic[3] = 'R';
    }

    bool hasChecksum() const {
        return (flags & FLAG_CHECKSUM_ENABLED) != 0;
    }

    // Calculate network latency (publish → receive)
    double networkLatencyUs() const {
        if (recv_time_ns == 0 || publish_time_ns == 0) {
            return 0.0;
        }
        return static_cast<double>(recv_time_ns - publish_time_ns) / 1000.0;
    }

    // Calculate event-to-receive latency
    double eventToReceiveUs() const {
        if (recv_time_ns == 0 || event_time_ns == 0) {
            return 0.0;
        }
        return static_cast<double>(recv_time_ns - event_time_ns) / 1000.0;
    }
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 64, "MessageHeader must be 64 bytes");

/**
 * Complete Message Buffer
 *
 * Structure:
 * - Header: 64 bytes (wire format)
 * - Payload: up to 4096 bytes (wire format)
 * - Metadata: Pool management (NOT in wire format)
 *
 * Total size: ~4.2KB per buffer
 */
struct MessageBuffer {
    // Wire format data
    MessageHeader header;                    // 64 bytes
    uint8_t payload[MAX_PAYLOAD_SIZE];       // 4096 bytes

    // Pool management metadata (not part of wire format)
    std::atomic<bool> in_use{false};         // Buffer allocation state
    uint32_t actual_payload_length{0};       // Actual payload size
    int64_t worker_dequeue_time_ns{0};       // Worker dequeue timestamp
    uint32_t padding_[3];                    // Padding for alignment

    // Constructor
    MessageBuffer() {
        reset();
    }

    // Reset buffer to initial state
    void reset() {
        memset(&header, 0, sizeof(header));
        actual_payload_length = 0;
        worker_dequeue_time_ns = 0;
        // Don't reset in_use - managed by pool
    }

    // Get total wire format size
    size_t wireSize() const {
        return sizeof(MessageHeader) + actual_payload_length;
    }

    // Get payload pointer
    const uint8_t* getPayload() const {
        return payload;
    }

    uint8_t* getPayload() {
        return payload;
    }

    // Copy from Aeron buffer (called by subscriber thread)
    void copyFromAeron(const uint8_t* aeron_buffer, size_t length) {
        // Copy header
        size_t header_size = std::min(length, sizeof(MessageHeader));
        memcpy(&header, aeron_buffer, header_size);

        // Copy payload
        if (length > sizeof(MessageHeader)) {
            size_t payload_size = std::min(
                length - sizeof(MessageHeader),
                MAX_PAYLOAD_SIZE
            );
            memcpy(payload, aeron_buffer + sizeof(MessageHeader), payload_size);
            actual_payload_length = static_cast<uint32_t>(payload_size);
        } else {
            actual_payload_length = 0;
        }
    }

    // Validate message integrity
    bool validate() const {
        // Check magic
        if (!header.isValid()) {
            return false;
        }

        // Check version
        if (header.version == 0 || header.version > 100) {
            return false;
        }

        // Check message length
        if (header.message_length > sizeof(MessageHeader) + MAX_PAYLOAD_SIZE) {
            return false;
        }

        // Verify checksum if enabled
        if (header.hasChecksum()) {
            // Calculate expected CRC32
            uint32_t expected_crc = calculateMessageCRC32(&header, payload, actual_payload_length);

            // Compare with stored checksum
            if (header.checksum != expected_crc) {
                // Checksum mismatch - message corrupted
                return false;
            }
        }

        return true;
    }

    // Calculate processing latency (receive → worker dequeue)
    double queuingLatencyUs() const {
        if (worker_dequeue_time_ns == 0 || header.recv_time_ns == 0) {
            return 0.0;
        }
        return static_cast<double>(worker_dequeue_time_ns - header.recv_time_ns) / 1000.0;
    }
};

// Calculate buffer size
constexpr size_t MESSAGE_BUFFER_SIZE = sizeof(MessageBuffer);

// Helper function to get current time in nanoseconds
inline int64_t getCurrentTimeNanos() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

// Implementation of calculateMessageCRC32 (declared earlier)
inline uint32_t calculateMessageCRC32(const MessageHeader* header, const uint8_t* payload, uint32_t payload_length) {
    // Create a temporary header copy with checksum = 0
    MessageHeader temp_header;
    memcpy(&temp_header, header, sizeof(MessageHeader));
    temp_header.checksum = 0;

    // Calculate CRC32 of header (with checksum = 0)
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&temp_header);

    for (size_t i = 0; i < sizeof(MessageHeader); ++i) {
        uint8_t index = (crc ^ header_bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ CRC32_TABLE[index];
    }

    // Continue with payload
    for (size_t i = 0; i < payload_length; ++i) {
        uint8_t index = (crc ^ payload[i]) & 0xFF;
        crc = (crc >> 8) ^ CRC32_TABLE[index];
    }

    return ~crc;
}

} // namespace example
} // namespace aeron

#endif // AERON_EXAMPLE_MESSAGE_BUFFER_H
