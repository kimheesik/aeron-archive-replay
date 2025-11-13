#include "AeronSubscriber.h"
#include "AeronConfig.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <cstring>

namespace aeron {
namespace example {

AeronSubscriber::AeronSubscriber()
    : running_(false)
    , message_count_(0)
    , driver_pid_(-1)
    , latency_sum_(0.0)
    , latency_min_(0.0)
    , latency_max_(0.0)
    , latency_count_(0) {
    config_.aeron_dir = AeronConfig::AERON_DIR;
    config_.use_embedded_driver = false;
}

AeronSubscriber::AeronSubscriber(const SubscriberConfig& config)
    : config_(config)
    , running_(false)
    , message_count_(0)
    , driver_pid_(-1)
    , latency_sum_(0.0)
    , latency_min_(0.0)
    , latency_max_(0.0)
    , latency_count_(0) {
}

AeronSubscriber::~AeronSubscriber() {
    shutdown();
}

bool AeronSubscriber::startEmbeddedDriver() {
    std::cout << "Starting embedded C Media Driver..." << std::endl;
    std::cout << "  Aeron directory: " << config_.aeron_dir << std::endl;

    // Fork 프로세스
    driver_pid_ = fork();

    if (driver_pid_ < 0) {
        std::cerr << "Failed to fork MediaDriver process: " << strerror(errno) << std::endl;
        return false;
    }

    if (driver_pid_ == 0) {
        // 자식 프로세스: C Media Driver 실행
        const char* aeronmd_path = "/home/hesed/aeron/bin/aeronmd";

        // C Media Driver 실행 (Java 대신 C++)
        execlp(aeronmd_path, "aeronmd",
               (std::string("-Daeron.dir=") + config_.aeron_dir).c_str(),
               "-Daeron.threading.mode=SHARED",
               nullptr);

        // execlp 실패 시
        std::cerr << "Failed to exec C MediaDriver: " << strerror(errno) << std::endl;
        std::cerr << "Make sure " << aeronmd_path << " exists and is executable" << std::endl;
        exit(1);
    }

    // 부모 프로세스: MediaDriver가 준비될 때까지 대기
    std::cout << "C Media Driver started with PID: " << driver_pid_ << std::endl;
    return waitForDriverReady();
}

bool AeronSubscriber::waitForDriverReady() {
    std::cout << "Waiting for MediaDriver to be ready..." << std::endl;

    // CnC 파일이 생성될 때까지 대기
    std::string cnc_file = config_.aeron_dir + "/cnc.dat";
    int max_retries = 50;  // 5초 대기 (50 * 100ms)

    for (int i = 0; i < max_retries; ++i) {
        struct stat buffer;
        if (stat(cnc_file.c_str(), &buffer) == 0) {
            std::cout << "MediaDriver is ready (CnC file found)" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 추가 안정화 시간
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 자식 프로세스 상태 확인
        int status;
        pid_t result = waitpid(driver_pid_, &status, WNOHANG);
        if (result != 0) {
            std::cerr << "MediaDriver process died unexpectedly" << std::endl;
            driver_pid_ = -1;
            return false;
        }
    }

    std::cerr << "Timeout waiting for MediaDriver to be ready" << std::endl;
    stopEmbeddedDriver();
    return false;
}

void AeronSubscriber::stopEmbeddedDriver() {
    if (driver_pid_ > 0) {
        std::cout << "Stopping embedded MediaDriver (PID: " << driver_pid_ << ")..." << std::endl;

        // SIGTERM 전송
        kill(driver_pid_, SIGTERM);

        // 정상 종료 대기 (최대 3초)
        int max_wait = 30;
        for (int i = 0; i < max_wait; ++i) {
            int status;
            pid_t result = waitpid(driver_pid_, &status, WNOHANG);
            if (result != 0) {
                std::cout << "MediaDriver stopped gracefully" << std::endl;
                driver_pid_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 강제 종료
        std::cout << "MediaDriver did not stop gracefully, sending SIGKILL..." << std::endl;
        kill(driver_pid_, SIGKILL);
        waitpid(driver_pid_, nullptr, 0);
        driver_pid_ = -1;
    }
}

bool AeronSubscriber::initialize() {
    try {
        std::cout << "Initializing Subscriber..." << std::endl;

        // Embedded MediaDriver 시작 (설정된 경우)
        if (config_.use_embedded_driver) {
            if (!startEmbeddedDriver()) {
                std::cerr << "Failed to start embedded MediaDriver" << std::endl;
                return false;
            }
        }

        // Aeron Context 설정
        context_ = std::make_shared<aeron::Context>();
        context_->aeronDir(config_.aeron_dir);

        // Aeron 인스턴스 생성
        aeron_ = aeron::Aeron::connect(*context_);
        std::cout << "Connected to Aeron" << std::endl;
        
        // Archive Context 설정 (Publisher 서버의 Archive에 연결)
        archive_context_ = std::make_shared<aeron::archive::client::Context>();
        archive_context_->aeron(aeron_);

        // Archive Control Channel 설정 (config에서 지정되었으면 사용, 아니면 기본값)
        const char* control_channel = config_.archive_control_channel.empty()
            ? AeronConfig::ARCHIVE_CONTROL_REQUEST_CHANNEL
            : config_.archive_control_channel.c_str();

        archive_context_->controlRequestChannel(control_channel);
        archive_context_->controlResponseChannel(AeronConfig::ARCHIVE_CONTROL_RESPONSE_CHANNEL);

        std::cout << "Archive control channel: " << control_channel << std::endl;
        
        // Archive 연결
        archive_ = aeron::archive::client::AeronArchive::connect(*archive_context_);
        std::cout << "Connected to Archive" << std::endl;
        
        // ReplayToLive Handler 생성
        replay_to_live_handler_ = std::make_unique<ReplayToLiveHandler>(
            aeron_,
            archive_
        );
        
        running_ = true;
        std::cout << "Subscriber initialized successfully" << std::endl;
        
        return true;
        
    } catch (const aeron::util::SourcedException& e) {
        std::cerr << "Failed to initialize Subscriber: " << e.what() 
                  << " at " << e.where() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize Subscriber: " << e.what() << std::endl;
        return false;
    }
}

bool AeronSubscriber::startLive() {
    std::cout << "Starting in LIVE mode..." << std::endl;

    const std::string& channel = config_.subscription_channel.empty()
        ? AeronConfig::SUBSCRIPTION_CHANNEL
        : config_.subscription_channel;

    return replay_to_live_handler_->startLive(
        channel,
        config_.subscription_stream_id
    );
}

bool AeronSubscriber::startReplay(int64_t startPosition) {
    std::cout << "Starting in REPLAY mode from position: " << startPosition << std::endl;

    const std::string& channel = config_.subscription_channel.empty()
        ? AeronConfig::SUBSCRIPTION_CHANNEL
        : config_.subscription_channel;

    return replay_to_live_handler_->startReplay(
        channel,
        config_.subscription_stream_id,
        startPosition
    );
}

void AeronSubscriber::handleMessage(
    const uint8_t* buffer,
    size_t length,
    int64_t position) {

    // ① 수신 즉시 타임스탬프 기록 (최우선!)
    auto recv_now = std::chrono::system_clock::now().time_since_epoch();
    auto recv_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(recv_now).count();

    message_count_++;

    // ② 메시지에서 전송 타임스탬프 추출
    std::string message(reinterpret_cast<const char*>(buffer), length);
    long long send_timestamp = 0;

    // "Message 123 at 1234567890" 형식에서 타임스탬프 추출
    size_t at_pos = message.find(" at ");
    if (at_pos != std::string::npos) {
        send_timestamp = std::stoll(message.substr(at_pos + 4));

        // ③ 레이턴시 계산 (마이크로초)
        double latency_us = (recv_timestamp - send_timestamp) / 1000.0;

        // ④ 통계 수집
        latency_sum_ += latency_us;
        latency_count_++;

        if (latency_min_ == 0.0 || latency_us < latency_min_) {
            latency_min_ = latency_us;
        }
        if (latency_us > latency_max_) {
            latency_max_ = latency_us;
        }

        // ⑤ 주기적으로 통계 출력 (100개마다)
        if (message_count_ % 100 == 0) {
            printLatencyStats();
        }
    } else {
        // 타임스탬프가 없는 메시지 (로깅만)
        if (message_count_ % 100 == 0) {
            auto mode = replay_to_live_handler_->getMode();
            const char* mode_str = (mode == SubscriptionMode::REPLAY) ? "REPLAY" :
                                   (mode == SubscriptionMode::TRANSITIONING) ? "TRANSITIONING" :
                                   "LIVE";

            std::cout << "[" << mode_str << "] Received " << message_count_
                      << " messages at position " << position << std::endl;
        }
    }
}

void AeronSubscriber::run() {
    std::cout << "Subscriber running. Press Ctrl+C to exit." << std::endl;
    
    while (running_) {
        int fragments = replay_to_live_handler_->poll(
            [this](const uint8_t* buffer, size_t length, int64_t position) {
                handleMessage(buffer, length, position);
            },
            10  // fragmentLimit
        );
        
        if (fragments == 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(AeronConfig::IDLE_SLEEP_MS));
        }
    }
}

void AeronSubscriber::printLatencyStats() {
    if (latency_count_ == 0) {
        return;
    }

    double avg_latency = latency_sum_ / latency_count_;

    std::cout << "\n========================================" << std::endl;
    std::cout << "Latency Statistics (" << latency_count_ << " samples)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Min:     " << std::fixed << std::setprecision(2) << latency_min_ << " μs" << std::endl;
    std::cout << "Max:     " << latency_max_ << " μs" << std::endl;
    std::cout << "Average: " << avg_latency << " μs" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void AeronSubscriber::shutdown() {
    std::cout << "Shutting down Subscriber..." << std::endl;

    running_ = false;

    if (replay_to_live_handler_) {
        replay_to_live_handler_->shutdown();
    }

    // 최종 레이턴시 통계 출력
    if (latency_count_ > 0) {
        std::cout << "\n=== FINAL STATISTICS ===" << std::endl;
        printLatencyStats();
    }

    replay_to_live_handler_.reset();
    archive_.reset();
    aeron_.reset();

    // Embedded MediaDriver 정리
    if (config_.use_embedded_driver) {
        stopEmbeddedDriver();
    }

    std::cout << "Subscriber shutdown complete. Total messages: "
              << message_count_ << std::endl;
}

} // namespace example
} // namespace aeron
