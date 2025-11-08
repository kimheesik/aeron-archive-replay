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
