# 분산 환경 설정 가이드

**최종 업데이트**: 2025-11-13
**구성**: Multicast + Embedded MediaDriver in Subscriber

---

## 개요

이 문서는 Publisher와 Subscriber가 **다른 서버**에 위치한 분산 환경 설정 가이드입니다.

### 아키텍처

```
┌─────────────────────────────────────────┐
│  Publisher 서버 (예: 192.168.1.10)       │
│  ┌──────────────────────────────────┐   │
│  │ Java ArchivingMediaDriver        │   │
│  │ - Recording                      │   │
│  │ - Archive: /pub/archive          │   │
│  │ - Control: 0.0.0.0:8010         │   │
│  └──────────────────────────────────┘   │
│  ┌──────────────────────────────────┐   │
│  │ C++ Publisher                    │   │
│  │ - Multicast 224.0.1.1:40456     │───┐│
│  └──────────────────────────────────┘   ││
└──────────────────────────────────────────┘│
                                           │ Multicast
              ┌────────────────────────────┼───────────────┐
              ▼                            ▼               ▼
┌────────────────────────────┐  ┌────────────────────────┐
│ Subscriber 서버 #1         │  │ Subscriber 서버 #2     │
│ (192.168.1.20)             │  │ (192.168.1.21)         │
│ ┌────────────────────────┐ │  │ ┌────────────────────┐ │
│ │ C++ Subscriber         │ │  │ │ C++ Subscriber     │ │
│ │ - Embedded MediaDriver │ │  │ │ - Embedded Driver  │ │
│ │ - Multicast 수신       │ │  │ │ - Multicast 수신   │ │
│ │ - Replay: 원격 Archive │ │  │ │ - Replay: 원격     │ │
│ └────────────────────────┘ │  │ └────────────────────┘ │
└────────────────────────────┘  └────────────────────────┘
```

### 주요 특징

✅ **Subscriber: Embedded MediaDriver**
- Java 프로세스 불필요
- 단일 C++ 실행 파일로 작동
- 자동으로 MediaDriver fork & 관리

✅ **Multicast 통신**
- 1:N 효율적인 메시지 전송
- 네트워크 트래픽 최소화

✅ **원격 Archive 접속**
- Replay 시 Publisher 서버 Archive에 연결
- Subscriber 서버에 Archive 불필요

---

## 사전 준비

### 1. 네트워크 요구사항

#### Multicast 지원 확인
```bash
# Multicast route 확인
ip route show | grep 224

# 없으면 추가
sudo ip route add 224.0.0.0/4 dev eth0
```

#### 방화벽 설정
```bash
# Publisher 서버
sudo ufw allow 8010/udp  # Archive Control
sudo ufw allow from 224.0.0.0/4  # Multicast

# Subscriber 서버
sudo ufw allow from 224.0.0.0/4  # Multicast
```

### 2. 소프트웨어 요구사항

**Publisher 서버**:
- Java 17+
- Aeron SDK (Java)
- C++ Publisher 애플리케이션

**Subscriber 서버**:
- Java 17+ (embedded MediaDriver용)
- C++ Subscriber 애플리케이션
- **외부 MediaDriver 불필요**

---

## 설정 단계

### Step 1: AeronConfig.h 설정 (이미 완료)

파일: `common/include/AeronConfig.h`

```cpp
class AeronConfig {
public:
    // Aeron 디렉토리 (tmpfs 권장)
    static constexpr const char* AERON_DIR = "/dev/shm/aeron";

    // Archive Control (Publisher 서버 주소)
    // 분산 환경: Publisher 서버 IP로 변경
    static constexpr const char* ARCHIVE_CONTROL_REQUEST_CHANNEL =
        "aeron:udp?endpoint=192.168.1.10:8010";  // ⬅️ Publisher IP

    // Publication/Subscription (Multicast)
    static constexpr const char* PUBLICATION_CHANNEL =
        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
    static constexpr const char* SUBSCRIPTION_CHANNEL =
        "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
};
```

**중요**: `ARCHIVE_CONTROL_REQUEST_CHANNEL`을 Publisher 서버 IP로 변경!

또는 실행 시 옵션으로 지정:
```bash
./subscriber/aeron_subscriber --embedded \
  --archive-control "aeron:udp?endpoint=192.168.1.10:8010"
```

### Step 2: 빌드

**모든 서버에서 동일하게 빌드**:

```bash
cd /home/hesed/devel/aeron/build
cmake ..
make -j$(nproc)
```

빌드 결과:
- `build/publisher/aeron_publisher`
- `build/subscriber/aeron_subscriber`

---

## 실행 방법

### 🖥️ Publisher 서버 (192.168.1.10)

#### Terminal 1: ArchivingMediaDriver

```bash
cd /home/hesed/devel/aeron
./scripts/start_archive_driver.sh
```

**중요**: `start_archive_driver.sh`에서 control channel 수정:
```bash
-Daeron.archive.control.channel=aeron:udp?endpoint=0.0.0.0:8010
```
(`localhost` → `0.0.0.0`로 변경하여 외부 접속 허용)

#### Terminal 2: Publisher

```bash
cd /home/hesed/devel/aeron/build
./publisher/aeron_publisher

# 대화형 명령:
start  # Recording 시작
stop   # Recording 중지
quit   # 종료
```

### 🖥️ Subscriber 서버 #1 (192.168.1.20)

#### Standalone 실행 (Embedded MediaDriver) ⭐ 권장

```bash
cd /home/hesed/devel/aeron/build

# Live 모드
./subscriber/aeron_subscriber --embedded

# Replay 모드 (position 0부터)
./subscriber/aeron_subscriber --embedded --replay 0

# Archive 주소 명시 (AeronConfig와 다른 경우)
./subscriber/aeron_subscriber --embedded \
  --archive-control "aeron:udp?endpoint=192.168.1.10:8010"
```

**특징**:
- ✅ **단일 프로세스** (Java MediaDriver 자동 fork)
- ✅ 종료 시 자동 정리
- ✅ 별도 터미널 불필요

### 🖥️ Subscriber 서버 #2 (192.168.1.21)

동일하게 실행:
```bash
./subscriber/aeron_subscriber --embedded
```

여러 Subscriber가 **동시에** 같은 Multicast 메시지를 수신합니다.

---

## 명령어 옵션

### Subscriber CLI

```bash
./subscriber/aeron_subscriber [OPTIONS]

Options:
  --aeron-dir <path>           Aeron directory (default: /dev/shm/aeron)
  --embedded                   Use embedded MediaDriver ⭐
  --archive-control <channel>  Archive control channel (override AeronConfig)
  --replay <position>          Start in replay mode from position
  -h, --help                   Show help
```

### 예제

```bash
# 1. Live 모드 (embedded driver)
./subscriber/aeron_subscriber --embedded

# 2. Live 모드 (외부 driver)
./subscriber/aeron_subscriber

# 3. Replay 모드 (embedded)
./subscriber/aeron_subscriber --embedded --replay 0

# 4. 분산 환경 (Publisher: 192.168.1.10)
./subscriber/aeron_subscriber --embedded \
  --archive-control "aeron:udp?endpoint=192.168.1.10:8010"

# 5. 커스텀 aeron-dir
./subscriber/aeron_subscriber --embedded \
  --aeron-dir /custom/path/aeron
```

---

## 로컬 테스트 (단일 서버)

분산 환경이 없어도 로컬에서 테스트 가능합니다.

### AeronConfig.h를 Localhost로 변경

```cpp
// 테스트용 Localhost 설정
static constexpr const char* PUBLICATION_CHANNEL =
    AeronConfig::LOCALHOST_PUBLICATION_CHANNEL;  // localhost:40456
static constexpr const char* SUBSCRIPTION_CHANNEL =
    AeronConfig::LOCALHOST_SUBSCRIPTION_CHANNEL;
```

또는 원본 유지하고:

```bash
# Publisher: 외부 ArchivingMediaDriver
Terminal 1: ./scripts/start_archive_driver.sh
Terminal 2: ./publisher/aeron_publisher

# Subscriber: embedded MediaDriver
Terminal 3: ./subscriber/aeron_subscriber --embedded
```

Multicast는 localhost에서도 작동합니다 (loopback).

---

## 트러블슈팅

### 1. "Failed to fork MediaDriver process"

**원인**: fork 권한 문제 또는 리소스 부족

**해결**:
```bash
# ulimit 확인
ulimit -u

# 증가
ulimit -u 4096
```

### 2. "Timeout waiting for MediaDriver to be ready"

**원인**: Java MediaDriver 실행 실패

**디버그**:
```bash
# Java 설치 확인
java -version

# Aeron JAR 경로 확인
ls -la /home/hesed/aeron/bin/aeron-all-1.50.0-SNAPSHOT.jar

# 수동 실행 테스트
java -cp /home/hesed/aeron/bin/aeron-all-1.50.0-SNAPSHOT.jar \
  -Daeron.dir=/dev/shm/aeron \
  io.aeron.driver.MediaDriver
```

**해결**: `AeronSubscriber.cpp:58` 경로 수정
```cpp
const char* aeron_jar = "/path/to/your/aeron-all.jar";  // ⬅️ 수정
```

### 3. Multicast 메시지 수신 안 됨

**확인**:
```bash
# Multicast route
ip route show | grep 224

# 네트워크 인터페이스
ip addr show

# Multicast 테스트 (Publisher 서버)
sudo tcpdump -i eth0 'host 224.0.1.1'
```

**해결**:
```bash
# Multicast route 추가
sudo ip route add 224.0.0.0/4 dev eth0

# 또는 특정 인터페이스 지정 (AeronConfig.h)
static constexpr const char* PUBLICATION_CHANNEL =
    "aeron:udp?endpoint=224.0.1.1:40456|interface=192.168.1.10";
```

### 4. "Failed to connect to Archive"

**원인**: Publisher 서버 Archive에 접속 불가

**확인**:
```bash
# Publisher 서버에서 포트 확인
netstat -tuln | grep 8010

# Subscriber 서버에서 연결 테스트
telnet 192.168.1.10 8010
```

**해결**:
- Publisher 서버 방화벽 확인
- Archive Control Channel을 `0.0.0.0:8010`으로 변경 (localhost 아님)

### 5. Embedded MediaDriver 종료 안 됨

**확인**:
```bash
# MediaDriver 프로세스 확인
ps aux | grep MediaDriver
```

**강제 종료**:
```bash
pkill -9 -f "io.aeron.driver.MediaDriver"
```

---

## 성능 최적화

### 1. Aeron 디렉토리 위치

**권장**: `/dev/shm/aeron` (tmpfs, RAM 기반)

```bash
# 각 서버에서 확인
mount | grep tmpfs

# 권한 설정
sudo chmod 1777 /dev/shm
```

### 2. Threading Mode

**현재**: SHARED (기본)
**고성능**: DEDICATED

```cpp
// AeronSubscriber.cpp:64 수정
"-Daeron.threading.mode=DEDICATED",
```

### 3. 네트워크 버퍼 크기

```bash
# 송신 버퍼
sudo sysctl -w net.core.wmem_max=2097152

# 수신 버퍼
sudo sysctl -w net.core.rmem_max=2097152
```

---

## 비교: Embedded vs External MediaDriver

| 항목 | Embedded (권장) | External |
|------|-----------------|----------|
| **Java 프로세스** | 자동 fork | 별도 실행 필요 |
| **터미널** | 1개 | 2개 (MediaDriver + Subscriber) |
| **관리** | 자동 정리 | 수동 정리 |
| **리소스** | 약간 높음 | 약간 낮음 |
| **사용 편의성** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |

**결론**: 대부분의 경우 **Embedded 모드 권장**

---

## 다음 단계

### 추가 개선 사항

1. **Replay Channel 동적 설정**
   - 현재: `localhost:40457` (Subscriber 로컬)
   - 분산 환경: Subscriber IP로 설정 필요
   - 향후: `--replay-channel` 옵션 추가

2. **Health Check**
   - MediaDriver 상태 모니터링
   - 자동 재시작 로직

3. **로깅**
   - MediaDriver 출력을 파일로 리다이렉션
   - 구조화된 로깅

4. **서비스화**
   - systemd unit 파일 작성
   - 자동 시작/재시작

---

## 참고 문서

- `CLAUDE.md` - 프로젝트 전체 가이드
- `PROGRESS.md` - 진행 상황
- `TEST_REPORT.md` - 테스트 보고서
- Aeron Wiki: https://github.com/real-logic/aeron/wiki

---

## 요약

### Publisher 서버
```bash
# 1. ArchivingMediaDriver
./scripts/start_archive_driver.sh

# 2. Publisher
./publisher/aeron_publisher
```

### Subscriber 서버
```bash
# 단일 명령 (Embedded MediaDriver)
./subscriber/aeron_subscriber --embedded
```

### 핵심
- ✅ Subscriber는 **Java 프로세스 불필요** (embedded)
- ✅ Multicast로 **1:N 효율적 통신**
- ✅ Replay는 **Publisher Archive에 원격 접속**

**완료!** 🎉
