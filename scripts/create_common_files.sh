#!/bin/bash

PROJECT_DIR="/home/hesed/devel/aeron"
COMMON_DIR="${PROJECT_DIR}/common"

echo "Creating common module files..."

# 디렉토리 생성
mkdir -p ${COMMON_DIR}/include
mkdir -p ${COMMON_DIR}/src

# Logger.h 생성
cat > ${COMMON_DIR}/include/Logger.h << 'EOF'
#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace aeron {
namespace example {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void setLevel(LogLevel level) {
        current_level_ = level;
    }
    
    static void debug(const std::string& message) {
        log(LogLevel::DEBUG, message);
    }
    
    static void info(const std::string& message) {
        log(LogLevel::INFO, message);
    }
    
    static void warn(const std::string& message) {
        log(LogLevel::WARN, message);
    }
    
    static void error(const std::string& message) {
        log(LogLevel::ERROR, message);
    }

private:
    static LogLevel current_level_;
    
    static void log(LogLevel level, const std::string& message) {
        if (level < current_level_) {
            return;
        }
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << levelToString(level) << "] " << message;
        
        std::cout << oss.str() << std::endl;
    }
    
    static std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};

} // namespace example
} // namespace aeron

#endif // LOGGER_H
EOF

# Logger.cpp 생성
cat > ${COMMON_DIR}/src/Logger.cpp << 'EOF'
#include "Logger.h"

namespace aeron {
namespace example {

LogLevel Logger::current_level_ = LogLevel::INFO;

} // namespace example
} // namespace aeron
EOF

# AeronConfig.cpp 생성
cat > ${COMMON_DIR}/src/AeronConfig.cpp << 'EOF'
#include "AeronConfig.h"

namespace aeron {
namespace example {

// AeronConfig는 모두 static constexpr이므로 
// 구현 파일에는 특별히 정의할 내용이 없음

} // namespace example
} // namespace aeron
EOF

# AeronConfig.h는 이미 정의되어 있다고 가정
# 만약 없다면 생성
if [ ! -f ${COMMON_DIR}/include/AeronConfig.h ]; then
    cat > ${COMMON_DIR}/include/AeronConfig.h << 'EOF'
#ifndef AERON_CONFIG_H
#define AERON_CONFIG_H

#include <string>

namespace aeron {
namespace example {

class AeronConfig {
public:
    // Aeron 디렉토리
    static constexpr const char* AERON_DIR = "/dev/shm/aeron";
    
    // Archive Control 채널 (Host1)
    static constexpr const char* ARCHIVE_CONTROL_REQUEST_CHANNEL = 
        "aeron:udp?endpoint=localhost:8010";
    static constexpr const char* ARCHIVE_CONTROL_RESPONSE_CHANNEL = 
        "aeron:udp?endpoint=localhost:8011";
    
    // Publication 채널 (멀티캐스트)
    static constexpr const char* PUBLICATION_CHANNEL = 
        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr int PUBLICATION_STREAM_ID = 10;
    
    // Live Subscription 채널
    static constexpr const char* SUBSCRIPTION_CHANNEL = 
        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr int SUBSCRIPTION_STREAM_ID = 10;
    
    // Replay 채널
    static constexpr const char* REPLAY_CHANNEL = 
        "aeron:udp?endpoint=localhost:0";
    static constexpr int REPLAY_STREAM_ID = 20;
    
    // 타임아웃
    static constexpr long long IDLE_SLEEP_MS = 1;
    static constexpr long long MESSAGE_TIMEOUT_NS = 10000000000LL; // 10초
};

} // namespace example
} // namespace aeron

#endif // AERON_CONFIG_H
EOF
fi

# CMakeLists.txt 업데이트
cat > ${COMMON_DIR}/CMakeLists.txt << 'EOF'
# 소스 파일 정의
set(COMMON_SOURCES
    src/Logger.cpp
    src/AeronConfig.cpp
)

# 헤더 파일 정의
set(COMMON_HEADERS
    include/Logger.h
    include/AeronConfig.h
)

# Static 라이브러리 생성
add_library(aeron_common STATIC
    ${COMMON_SOURCES}
    ${COMMON_HEADERS}
)

# Include 디렉토리 설정
target_include_directories(aeron_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
EOF

echo "Common module files created successfully!"
echo ""
echo "Created files:"
echo "  - ${COMMON_DIR}/include/Logger.h"
echo "  - ${COMMON_DIR}/include/AeronConfig.h"
echo "  - ${COMMON_DIR}/src/Logger.cpp"
echo "  - ${COMMON_DIR}/src/AeronConfig.cpp"
echo "  - ${COMMON_DIR}/CMakeLists.txt"

