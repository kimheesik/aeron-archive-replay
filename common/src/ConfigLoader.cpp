#include "ConfigLoader.h"
#include "AeronConfig.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>

namespace aeron {
namespace example {

// ============================================================================
// AeronSettings 구현
// ============================================================================

AeronSettings::AeronSettings() {
    // AeronConfig.h의 기본값 사용
    aeron_dir = AeronConfig::AERON_DIR;

    archive_control_request_channel = AeronConfig::ARCHIVE_CONTROL_REQUEST_CHANNEL;
    archive_control_response_channel = AeronConfig::ARCHIVE_CONTROL_RESPONSE_CHANNEL;

    publication_channel = AeronConfig::PUBLICATION_CHANNEL;
    publication_stream_id = AeronConfig::PUBLICATION_STREAM_ID;

    subscription_channel = AeronConfig::SUBSCRIPTION_CHANNEL;
    subscription_stream_id = AeronConfig::SUBSCRIPTION_STREAM_ID;

    replay_channel = AeronConfig::REPLAY_CHANNEL;
    replay_stream_id = AeronConfig::REPLAY_STREAM_ID;

    idle_sleep_ms = AeronConfig::IDLE_SLEEP_MS;
    message_timeout_ns = AeronConfig::MESSAGE_TIMEOUT_NS;
}

bool AeronSettings::validate(std::string& error_message) const {
    // Aeron 디렉토리 검증
    if (aeron_dir.empty()) {
        error_message = "aeron_dir is empty";
        return false;
    }

    // 채널 검증 (aeron: 프로토콜로 시작해야 함)
    auto validateChannel = [&](const std::string& channel, const std::string& name) {
        if (channel.empty()) {
            error_message = name + " is empty";
            return false;
        }
        if (channel.find("aeron:") != 0) {
            error_message = name + " must start with 'aeron:'";
            return false;
        }
        return true;
    };

    if (!validateChannel(archive_control_request_channel, "archive_control_request_channel"))
        return false;
    if (!validateChannel(publication_channel, "publication_channel"))
        return false;
    if (!validateChannel(subscription_channel, "subscription_channel"))
        return false;
    if (!validateChannel(replay_channel, "replay_channel"))
        return false;

    // Stream ID 검증 (양수)
    if (publication_stream_id <= 0) {
        error_message = "publication_stream_id must be positive";
        return false;
    }
    if (subscription_stream_id <= 0) {
        error_message = "subscription_stream_id must be positive";
        return false;
    }
    if (replay_stream_id <= 0) {
        error_message = "replay_stream_id must be positive";
        return false;
    }

    return true;
}

void AeronSettings::print() const {
    std::cout << "========================================" << std::endl;
    std::cout << "Aeron Configuration" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[aeron]" << std::endl;
    std::cout << "  dir = " << aeron_dir << std::endl;
    std::cout << "\n[archive]" << std::endl;
    std::cout << "  control_request_channel = " << archive_control_request_channel << std::endl;
    std::cout << "  control_response_channel = " << archive_control_response_channel << std::endl;
    std::cout << "\n[publication]" << std::endl;
    std::cout << "  channel = " << publication_channel << std::endl;
    std::cout << "  stream_id = " << publication_stream_id << std::endl;
    std::cout << "\n[subscription]" << std::endl;
    std::cout << "  channel = " << subscription_channel << std::endl;
    std::cout << "  stream_id = " << subscription_stream_id << std::endl;
    std::cout << "\n[replay]" << std::endl;
    std::cout << "  channel = " << replay_channel << std::endl;
    std::cout << "  stream_id = " << replay_stream_id << std::endl;
    std::cout << "\n[timeouts]" << std::endl;
    std::cout << "  idle_sleep_ms = " << idle_sleep_ms << std::endl;
    std::cout << "  message_timeout_ns = " << message_timeout_ns << std::endl;
    std::cout << "========================================" << std::endl;
}

// ============================================================================
// ConfigLoader 구현
// ============================================================================

AeronSettings ConfigLoader::loadDefault() {
    return AeronSettings();  // 기본값 사용
}

AeronSettings ConfigLoader::loadFromFile(const std::string& filepath) {
    std::cout << "Loading configuration from: " << filepath << std::endl;

    // INI 파일 파싱
    auto ini_data = parseINI(filepath);

    // 기본값으로 시작
    AeronSettings settings;

    // [aeron] 섹션
    if (ini_data.count("aeron")) {
        const auto& section = ini_data["aeron"];
        if (section.count("dir")) {
            settings.aeron_dir = section.at("dir");
        }
    }

    // [archive] 섹션
    if (ini_data.count("archive")) {
        const auto& section = ini_data["archive"];
        if (section.count("control_request_channel")) {
            settings.archive_control_request_channel = section.at("control_request_channel");
        }
        if (section.count("control_response_channel")) {
            settings.archive_control_response_channel = section.at("control_response_channel");
        }
    }

    // [publication] 섹션
    if (ini_data.count("publication")) {
        const auto& section = ini_data["publication"];
        if (section.count("channel")) {
            settings.publication_channel = section.at("channel");
        }
        if (section.count("stream_id")) {
            settings.publication_stream_id = parseInt(section.at("stream_id"), "publication.stream_id");
        }
    }

    // [subscription] 섹션
    if (ini_data.count("subscription")) {
        const auto& section = ini_data["subscription"];
        if (section.count("channel")) {
            settings.subscription_channel = section.at("channel");
        }
        if (section.count("stream_id")) {
            settings.subscription_stream_id = parseInt(section.at("stream_id"), "subscription.stream_id");
        }
    }

    // [replay] 섹션
    if (ini_data.count("replay")) {
        const auto& section = ini_data["replay"];
        if (section.count("channel")) {
            settings.replay_channel = section.at("channel");
        }
        if (section.count("stream_id")) {
            settings.replay_stream_id = parseInt(section.at("stream_id"), "replay.stream_id");
        }
    }

    // [timeouts] 섹션
    if (ini_data.count("timeouts")) {
        const auto& section = ini_data["timeouts"];
        if (section.count("idle_sleep_ms")) {
            settings.idle_sleep_ms = parseLongLong(section.at("idle_sleep_ms"), "timeouts.idle_sleep_ms");
        }
        if (section.count("message_timeout_ns")) {
            settings.message_timeout_ns = parseLongLong(section.at("message_timeout_ns"), "timeouts.message_timeout_ns");
        }
    }

    // 환경변수로 override
    overrideFromEnvironment(settings);

    // 검증
    std::string error_message;
    if (!settings.validate(error_message)) {
        throw std::runtime_error("Configuration validation failed: " + error_message);
    }

    std::cout << "Configuration loaded successfully" << std::endl;
    return settings;
}

void ConfigLoader::overrideFromEnvironment(AeronSettings& settings) {
    auto getEnv = [](const char* name) -> std::string {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    };

    // AERON_DIR
    std::string env_aeron_dir = getEnv("AERON_DIR");
    if (!env_aeron_dir.empty()) {
        std::cout << "  Override from env: AERON_DIR = " << env_aeron_dir << std::endl;
        settings.aeron_dir = env_aeron_dir;
    }

    // AERON_ARCHIVE_CONTROL
    std::string env_archive_control = getEnv("AERON_ARCHIVE_CONTROL");
    if (!env_archive_control.empty()) {
        std::cout << "  Override from env: AERON_ARCHIVE_CONTROL = " << env_archive_control << std::endl;
        settings.archive_control_request_channel = env_archive_control;
    }

    // AERON_PUBLICATION_CHANNEL
    std::string env_pub_channel = getEnv("AERON_PUBLICATION_CHANNEL");
    if (!env_pub_channel.empty()) {
        std::cout << "  Override from env: AERON_PUBLICATION_CHANNEL = " << env_pub_channel << std::endl;
        settings.publication_channel = env_pub_channel;
    }

    // AERON_SUBSCRIPTION_CHANNEL
    std::string env_sub_channel = getEnv("AERON_SUBSCRIPTION_CHANNEL");
    if (!env_sub_channel.empty()) {
        std::cout << "  Override from env: AERON_SUBSCRIPTION_CHANNEL = " << env_sub_channel << std::endl;
        settings.subscription_channel = env_sub_channel;
    }
}

std::map<std::string, std::map<std::string, std::string>>
ConfigLoader::parseINI(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + filepath);
    }

    std::map<std::string, std::map<std::string, std::string>> result;
    std::string current_section;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        line_number++;
        line = trim(line);

        // 빈 줄 또는 주석 무시
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // 섹션 헤더 [section]
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = trim(current_section);
            continue;
        }

        // Key = Value
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            std::cerr << "Warning: Invalid line " << line_number << ": " << line << std::endl;
            continue;
        }

        std::string key = trim(line.substr(0, equals_pos));
        std::string value = trim(line.substr(equals_pos + 1));

        if (current_section.empty()) {
            std::cerr << "Warning: Key outside section at line " << line_number << std::endl;
            continue;
        }

        result[current_section][key] = value;
    }

    file.close();
    return result;
}

std::string ConfigLoader::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

int ConfigLoader::parseInt(const std::string& str, const std::string& key) {
    try {
        return std::stoi(str);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse integer for '" + key + "': " + str);
    }
}

long long ConfigLoader::parseLongLong(const std::string& str, const std::string& key) {
    try {
        return std::stoll(str);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse long long for '" + key + "': " + str);
    }
}

void ConfigLoader::generateTemplate(const std::string& filepath,
                                     const std::string& template_type) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create config file: " + filepath);
    }

    file << "# Aeron Configuration File\n";
    file << "# Generated template: " << template_type << "\n";
    file << "#\n";
    file << "# Lines starting with # or ; are comments\n";
    file << "# Format: key = value\n";
    file << "\n";

    if (template_type == "local") {
        file << "[aeron]\n";
        file << "dir = /dev/shm/aeron\n";
        file << "\n";
        file << "[archive]\n";
        file << "control_request_channel = aeron:udp?endpoint=localhost:8010\n";
        file << "control_response_channel = aeron:udp?endpoint=localhost:0\n";
        file << "\n";
        file << "[publication]\n";
        file << "channel = aeron:udp?endpoint=localhost:40456\n";
        file << "stream_id = 10\n";
        file << "\n";
        file << "[subscription]\n";
        file << "channel = aeron:udp?endpoint=localhost:40456\n";
        file << "stream_id = 10\n";
        file << "\n";
        file << "[replay]\n";
        file << "channel = aeron:udp?endpoint=localhost:40457\n";
        file << "stream_id = 20\n";
        file << "\n";

    } else if (template_type == "distributed") {
        file << "# Distributed setup with multicast\n";
        file << "# Publisher server: 192.168.1.10\n";
        file << "# Subscriber servers: 192.168.1.20, 192.168.1.21, ...\n";
        file << "\n";
        file << "[aeron]\n";
        file << "dir = /dev/shm/aeron\n";
        file << "\n";
        file << "[archive]\n";
        file << "# Publisher server IP\n";
        file << "control_request_channel = aeron:udp?endpoint=192.168.1.10:8010\n";
        file << "control_response_channel = aeron:udp?endpoint=localhost:0\n";
        file << "\n";
        file << "[publication]\n";
        file << "# Multicast address\n";
        file << "channel = aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0\n";
        file << "stream_id = 10\n";
        file << "\n";
        file << "[subscription]\n";
        file << "channel = aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0\n";
        file << "stream_id = 10\n";
        file << "\n";
        file << "[replay]\n";
        file << "channel = aeron:udp?endpoint=localhost:40457\n";
        file << "stream_id = 20\n";
        file << "\n";

    } else {
        // production
        file << "# Production configuration\n";
        file << "\n";
        file << "[aeron]\n";
        file << "dir = /dev/shm/aeron\n";
        file << "\n";
        file << "[archive]\n";
        file << "control_request_channel = aeron:udp?endpoint=PUBLISHER_IP:8010\n";
        file << "control_response_channel = aeron:udp?endpoint=localhost:0\n";
        file << "\n";
        file << "[publication]\n";
        file << "channel = aeron:udp?endpoint=MULTICAST_IP:40456|interface=0.0.0.0\n";
        file << "stream_id = 10\n";
        file << "\n";
        file << "[subscription]\n";
        file << "channel = aeron:udp?endpoint=MULTICAST_IP:40456|interface=0.0.0.0\n";
        file << "stream_id = 10\n";
        file << "\n";
        file << "[replay]\n";
        file << "channel = aeron:udp?endpoint=localhost:40457\n";
        file << "stream_id = 20\n";
        file << "\n";
    }

    file << "[timeouts]\n";
    file << "idle_sleep_ms = 1\n";
    file << "message_timeout_ns = 10000000000\n";

    file.close();
    std::cout << "Template config file created: " << filepath << std::endl;
}

} // namespace example
} // namespace aeron
