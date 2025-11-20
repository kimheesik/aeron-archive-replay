#include "CheckpointManager.h"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iomanip>

namespace aeron {
namespace example {

CheckpointManager::CheckpointManager(const std::string& file, int flush_interval_sec)
    : checkpoint_file_(file)
    , flush_interval_(flush_interval_sec) {

    std::cout << "========================================" << std::endl;
    std::cout << "Initializing CheckpointManager" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  File: " << checkpoint_file_ << std::endl;
    std::cout << "  Flush interval: " << flush_interval_sec << " seconds" << std::endl;

    // Load existing checkpoint
    load();

    // Start background flush thread
    flush_thread_ = std::thread([this]() { flushLoop(); });

    std::cout << "  Background flush thread started" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

CheckpointManager::~CheckpointManager() {
    std::cout << "\nShutting down CheckpointManager..." << std::endl;

    // Stop background thread
    running_ = false;
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }

    // Final flush
    std::cout << "Performing final checkpoint flush..." << std::endl;
    flush();

    // Print statistics
    printStatistics();

    std::cout << "CheckpointManager shutdown complete" << std::endl;
}

void CheckpointManager::update(int64_t sequence, int64_t position, int64_t msg_count) {
    // Fast path: Atomic stores only (~10 ns total)
    // No locks, no I/O, no blocking

    data_.last_sequence_number.store(sequence, std::memory_order_relaxed);
    data_.last_position.store(position, std::memory_order_relaxed);
    data_.message_count.store(msg_count, std::memory_order_relaxed);
    data_.timestamp_ns.store(getCurrentTimeNanos(), std::memory_order_relaxed);

    // Background thread will flush periodically
}

void CheckpointManager::forceFlush() {
    flush();
}

int64_t CheckpointManager::getLastSequence() const {
    return data_.last_sequence_number.load(std::memory_order_relaxed);
}

int64_t CheckpointManager::getLastPosition() const {
    return data_.last_position.load(std::memory_order_relaxed);
}

int64_t CheckpointManager::getMessageCount() const {
    return data_.message_count.load(std::memory_order_relaxed);
}

int64_t CheckpointManager::getTimestamp() const {
    return data_.timestamp_ns.load(std::memory_order_relaxed);
}

void CheckpointManager::printStatistics() const {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Checkpoint Statistics" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Total flushes: " << flush_count_.load() << std::endl;
    std::cout << "  Flush failures: " << flush_failures_.load() << std::endl;
    std::cout << "  Last sequence: " << getLastSequence() << std::endl;
    std::cout << "  Last position: " << getLastPosition() << std::endl;
    std::cout << "  Message count: " << getMessageCount() << std::endl;
    std::cout << "========================================" << std::endl;
}

void CheckpointManager::flushLoop() {
    while (running_) {
        // Sleep for flush interval
        std::this_thread::sleep_for(flush_interval_);

        // Flush checkpoint to disk
        if (running_) {  // Check again after sleep
            flush();
        }
    }
}

void CheckpointManager::flush() {
    try {
        // 1. Read atomic values (consistent snapshot)
        //    Using relaxed ordering is safe here because we're the only consumer
        int64_t seq = data_.last_sequence_number.load(std::memory_order_relaxed);
        int64_t pos = data_.last_position.load(std::memory_order_relaxed);
        int64_t count = data_.message_count.load(std::memory_order_relaxed);
        int64_t ts = data_.timestamp_ns.load(std::memory_order_relaxed);

        // Skip flush if no data yet
        if (seq == 0 && pos == 0 && count == 0) {
            return;
        }

        // 2. Write to temporary file first (atomic rename pattern)
        std::string temp_file = checkpoint_file_ + ".tmp";

        std::ofstream ofs(temp_file, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            std::cerr << "ERROR: Failed to open checkpoint temp file: " << temp_file << std::endl;
            std::cerr << "       Error: " << std::strerror(errno) << std::endl;
            flush_failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Write magic number for validation
        const uint32_t MAGIC = 0x43484B50;  // "CHKP"
        ofs.write(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC));

        // Write version
        const uint16_t VERSION = 1;
        ofs.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));

        // Write padding for alignment
        const uint16_t PADDING = 0;
        ofs.write(reinterpret_cast<const char*>(&PADDING), sizeof(PADDING));

        // Write checkpoint data
        ofs.write(reinterpret_cast<const char*>(&seq), sizeof(seq));
        ofs.write(reinterpret_cast<const char*>(&pos), sizeof(pos));
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
        ofs.write(reinterpret_cast<const char*>(&ts), sizeof(ts));

        // Flush to OS buffer
        ofs.flush();

        if (!ofs) {
            std::cerr << "ERROR: Failed to write checkpoint data" << std::endl;
            flush_failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        ofs.close();

        // 3. Atomic rename (POSIX guarantees atomicity)
        //    This ensures that checkpoint file is never corrupted
        //    Even if process crashes during write
        if (std::rename(temp_file.c_str(), checkpoint_file_.c_str()) != 0) {
            std::cerr << "ERROR: Failed to rename checkpoint file" << std::endl;
            std::cerr << "       From: " << temp_file << std::endl;
            std::cerr << "       To: " << checkpoint_file_ << std::endl;
            std::cerr << "       Error: " << std::strerror(errno) << std::endl;
            flush_failures_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // 4. Success
        flush_count_.fetch_add(1, std::memory_order_relaxed);

        // Optional: fsync directory for maximum durability
        // This is commented out for performance, but can be enabled
        // if you need to guarantee checkpoint survives OS crash
        // syncDirectory(checkpoint_file_);

    } catch (const std::exception& e) {
        std::cerr << "ERROR: Checkpoint flush exception: " << e.what() << std::endl;
        flush_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void CheckpointManager::load() {
    std::ifstream ifs(checkpoint_file_, std::ios::binary);
    if (!ifs) {
        std::cout << "  No existing checkpoint found" << std::endl;
        std::cout << "  Starting from position 0" << std::endl;
        return;
    }

    try {
        // Read and validate magic number
        uint32_t magic;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (!ifs || magic != 0x43484B50) {
            std::cerr << "  WARNING: Invalid checkpoint file (bad magic number)" << std::endl;
            std::cerr << "  Starting from position 0" << std::endl;
            return;
        }

        // Read version
        uint16_t version;
        ifs.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1) {
            std::cerr << "  WARNING: Unsupported checkpoint version: " << version << std::endl;
            std::cerr << "  Starting from position 0" << std::endl;
            return;
        }

        // Read padding
        uint16_t padding;
        ifs.read(reinterpret_cast<char*>(&padding), sizeof(padding));

        // Read checkpoint data
        int64_t seq, pos, count, ts;
        ifs.read(reinterpret_cast<char*>(&seq), sizeof(seq));
        ifs.read(reinterpret_cast<char*>(&pos), sizeof(pos));
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
        ifs.read(reinterpret_cast<char*>(&ts), sizeof(ts));

        if (!ifs) {
            std::cerr << "  WARNING: Failed to read checkpoint data" << std::endl;
            std::cerr << "  Starting from position 0" << std::endl;
            return;
        }

        // Store loaded values
        data_.last_sequence_number.store(seq, std::memory_order_relaxed);
        data_.last_position.store(pos, std::memory_order_relaxed);
        data_.message_count.store(count, std::memory_order_relaxed);
        data_.timestamp_ns.store(ts, std::memory_order_relaxed);

        // Print loaded checkpoint
        std::cout << "  ✓ Loaded existing checkpoint:" << std::endl;
        std::cout << "    Sequence: " << seq << std::endl;
        std::cout << "    Position: " << pos << std::endl;
        std::cout << "    Messages: " << count << std::endl;

        // Calculate age
        int64_t now = getCurrentTimeNanos();
        int64_t age_sec = (now - ts) / 1000000000LL;
        std::cout << "    Age: " << age_sec << " seconds" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "  WARNING: Exception loading checkpoint: " << e.what() << std::endl;
        std::cerr << "  Starting from position 0" << std::endl;
    }
}

int64_t CheckpointManager::getCurrentTimeNanos() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

} // namespace example
} // namespace aeron
