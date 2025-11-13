#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <string>
#include <map>
#include <stdexcept>

namespace aeron {
namespace example {

/**
 * Aeron 설정을 담는 구조체
 * Config file, 환경변수, CLI 옵션에서 로드 가능
 */
struct AeronSettings {
    // Aeron 디렉토리
    std::string aeron_dir;

    // Archive Control 채널
    std::string archive_control_request_channel;
    std::string archive_control_response_channel;

    // Publication 설정
    std::string publication_channel;
    int publication_stream_id;

    // Subscription 설정
    std::string subscription_channel;
    int subscription_stream_id;

    // Replay 설정
    std::string replay_channel;
    int replay_stream_id;

    // Timeouts
    long long idle_sleep_ms;
    long long message_timeout_ns;

    // 기본값으로 초기화 (AeronConfig.h 값 사용)
    AeronSettings();

    // 설정 검증
    bool validate(std::string& error_message) const;

    // 설정 출력
    void print() const;
};

/**
 * INI 파일 및 환경변수에서 설정 로드
 */
class ConfigLoader {
public:
    /**
     * INI 파일에서 설정 로드
     * @param filepath INI 파일 경로
     * @return AeronSettings 구조체
     * @throws std::runtime_error 파일 없음 또는 파싱 에러
     */
    static AeronSettings loadFromFile(const std::string& filepath);

    /**
     * 기본값 로드 (AeronConfig.h의 값)
     * @return AeronSettings 구조체
     */
    static AeronSettings loadDefault();

    /**
     * 환경변수로 설정 override
     * @param settings 기존 설정 (in/out)
     */
    static void overrideFromEnvironment(AeronSettings& settings);

    /**
     * 설정 파일 샘플 생성
     * @param filepath 생성할 파일 경로
     * @param template_type "local", "distributed", "production"
     */
    static void generateTemplate(const std::string& filepath,
                                  const std::string& template_type = "local");

private:
    /**
     * INI 파일 파싱
     * @param filepath 파일 경로
     * @return 섹션 -> (키 -> 값) 맵
     */
    static std::map<std::string, std::map<std::string, std::string>>
        parseINI(const std::string& filepath);

    /**
     * 문자열 trim (공백 제거)
     */
    static std::string trim(const std::string& str);

    /**
     * 문자열을 int로 변환 (에러 처리 포함)
     */
    static int parseInt(const std::string& str, const std::string& key);

    /**
     * 문자열을 long long으로 변환
     */
    static long long parseLongLong(const std::string& str, const std::string& key);
};

} // namespace example
} // namespace aeron

#endif // CONFIG_LOADER_H
