# Aeron 메시징 시스템 개발 문서

## 📋 목차
1. [개요](#개요)
2. [개발 요건](#개발-요건)
3. [시스템 아키텍처](#시스템-아키텍처)
4. [개발 환경](#개발-환경)
5. [구현 내용](#구현-내용)
6. [빌드 및 설치](#빌드-및-설치)
7. [실행 방법](#실행-방법)
8. [테스트](#테스트)
9. [트러블슈팅](#트러블슈팅)
10. [API 참조](#api-참조)

---

## 개요

### 프로젝트 목적
Aeron 메시징 시스템을 기반으로 한 고성능 Publisher/Subscriber 구현 및 Recording/Replay 기능 제공

### 주요 기능
- **실시간 메시지 스트리밍**: Publisher에서 Subscriber로 실시간 데이터 전송
- **Recording 제어**: Publisher에서 스트림 녹화 시작/중지
- **Replay 기능**: 녹화된 데이터를 특정 위치부터 재생
- **Replay-to-Live 전환**: Replay 완료 후 자동으로 Live 스트림으로 전환

---

## 개발 요건

### 1. 개발 환경
| 항목 | 사양 |
|------|------|
| OS | Rocky Linux 8.10 |
| 언어 | C++ (Publisher/Subscriber), Java 17 (ArchivingMediaDriver) |
| Aeron 버전 | 1.50.1 |
| 컴파일러 | GCC 8+ / Clang 10+ |
| 빌드 도구 | CMake 3.15+ |

### 2. 기능 요구사항

#### Publisher
- 실시간 메시지 스트림 전송
- Recording 시작/중지 제어
- 사용자 명령어 인터페이스 (start/stop/quit)

#### Subscriber
- **Live 모드**: 실시간 스트림 수신
- **Replay 모드**: 
  - Sequence 번호(position) 기반 replay 시작
  - Replay 완료 후 자동으로 Live 스트림 전환
  - 끊김 없는 데이터 수신

#### Archive
- Java ArchivingMediaDriver 사용
- 스트림 데이터 저장 및 관리
- Recording 메타데이터 관리

### 3. 아키텍처 요구사항
- 1:N 데이터 pub/sub 구조
- UDP 유니캐스트 전송 (테스트 환경)
- Publisher와 Subscriber는 C++ Wrapper API 사용

---

## 시스템 아키텍처

### 전체 구조도

```
┌─────────────────────────────────────────────────────────────┐
│                         Host1                                │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Java ArchivingMediaDriver (Port 8010)               │   │
│  │  - MediaDriver                                        │   │
│  │  - Archive (Recording Storage)                        │   │
│  └──────────────────────────────────────────────────────┘   │
│                           ▲                                  │
│                           │ Archive Control                  │
│                           │                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Publisher (C++)                                      │   │
│  │  - Publication (UDP localhost:40456)                  │   │
│  │  - RecordingController                                │   │
│  │    • startRecording()                                 │   │
│  │    • stopRecording()                                  │   │
│  └──────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           │ UDP Stream                       │
│                           ▼                                  │
└───────────────────────────────────────────────────────────┬─┘
                            │                                 │
                            │                                 │
┌───────────────────────────▼─────────────────────────────────┘
│                         Host2                                │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Subscriber (C++)                                     │   │
│  │                                                        │   │
│  │  ┌──────────────────────────────────────────────┐    │   │
│  │  │  ReplayToLiveHandler                          │    │   │
│  │  │  - Replay Subscription                        │    │   │
│  │  │  - Live Subscription                          │    │   │
│  │  │  - Mode: REPLAY → TRANSITIONING → LIVE       │    │   │
│  │  └──────────────────────────────────────────────┘    │   │
│  │                                                        │   │
│  │  Archive Client (원격 접근 to Host1)                  │   │
│  └──────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────┘
```

### 데이터 흐름

#### Live Streaming
```
Publisher → [UDP Stream] → Subscriber (Live Mode)
     ↓
  Archive
  (Recording)
```

#### Replay to Live
```
Subscriber → Archive Client → Archive → [Replay Stream]
                                              ↓
                                    Subscriber (Replay Mode)
                                              ↓
                                    [Replay 완료 감지]
                                              ↓
                                    Subscriber (Live Mode)
                                              ↑
Publisher → [UDP Stream] ─────────────────────┘
```

---

## 개발 환경

### 디렉토리 구조

```
/home/hesed/
├── aeron/                                    # Aeron 1.50.1 소스 및 빌드
│   ├── bin/
│   │   └── aeron-all-1.50.0-SNAPSHOT.jar   # Java Archive Driver
│   ├── include/                             # C++ 헤더
│   └── lib/                                 # C++ 라이브러리
│
└── devel/aeron/                             # 개발 프로젝트
    ├── CMakeLists.txt                       # 루트 빌드 설정
    ├── common/                              # 공통 모듈
    │   ├── include/
    │   │   ├── AeronConfig.h               # 설정 상수
    │   │   └── Logger.h                    # 로깅 유틸
    │   └── src/
    │       ├── AeronConfig.cpp
    │       └── Logger.cpp
    │
    ├── publisher/                           # Publisher 모듈
    │   ├── include/
    │   │   ├── AeronPublisher.h
    │   │   └── RecordingController.h
    │   └── src/
    │       ├── AeronPublisher.cpp
    │       ├── RecordingController.cpp
    │       └── main.cpp
    │
    ├── subscriber/                          # Subscriber 모듈
    │   ├── include/
    │   │   ├── AeronSubscriber.h
    │   │   └── ReplayToLiveHandler.h
    │   └── src/
    │       ├── AeronSubscriber.cpp
    │       ├── ReplayToLiveHandler.cpp
    │       └── main.cpp
    │
    ├── scripts/                             # 실행 스크립트
    │   ├── start_archive_driver.sh
    │   ├── run_test.sh
    │   └── build.sh
    │
    └── build/                               # 빌드 출력
        ├── publisher/aeron_publisher
        └── subscriber/aeron_subscriber
```

### Aeron 설정

**파일: `/home/hesed/devel/aeron/common/include/AeronConfig.h`**

```cpp
class AeronConfig {
public:
    // Aeron 공유 메모리 디렉토리
    static constexpr const char* AERON_DIR = "/dev/shm/aeron";
    
    // Archive Control 채널 (Host1)
    static constexpr const char* ARCHIVE_CONTROL_REQUEST_CHANNEL = 
        "aeron:udp?endpoint=localhost:8010";
    static constexpr const char* ARCHIVE_CONTROL_RESPONSE_CHANNEL = 
        "aeron:udp?endpoint=localhost:0";
    
    // Publication/Subscription 채널 (UDP Unicast)
    static constexpr const char* PUBLICATION_CHANNEL = 
        "aeron:udp?endpoint=localhost:40456";
    static constexpr int PUBLICATION_STREAM_ID = 10;
    
    static constexpr const char* SUBSCRIPTION_CHANNEL = 
        "aeron:udp?endpoint=localhost:40456";
    static constexpr int SUBSCRIPTION_STREAM_ID = 10;
    
    // Replay 채널
    static constexpr const char* REPLAY_CHANNEL = 
        "aeron:udp?endpoint=localhost:0";
    static constexpr int REPLAY_STREAM_ID = 20;
    
    // 타임아웃
    static constexpr long long IDLE_SLEEP_MS = 1;
    static constexpr long long MESSAGE_TIMEOUT_NS = 10000000000LL;
};
```

---

## 구현 내용

### 1. Publisher 구현

#### 주요 클래스

**AeronPublisher**
- 역할: 메시지 발행 및 Recording 제어
- 주요 메서드:
  - `initialize()`: Aeron 및 Archive 연결
  - `publish()`: 메시지 전송
  - `startRecording()`: Recording 시작
  - `stopRecording()`: Recording 종료
  - `run()`: 메시지 발행 스레드 및 명령 처리

**RecordingController**
- 역할: Archive Recording 제어
- 주요 메서드:
  - `startRecording()`: Archive에 Recording 시작 요청
  - `stopRecording()`: Archive에 Recording 중지 요청
  - `isRecording()`: Recording 상태 확인

#### 메시지 발행 흐름

```cpp
// 1. AtomicBuffer로 래핑
aeron::concurrent::AtomicBuffer atomic_buffer(
    const_cast<uint8_t*>(buffer), length);

// 2. Publication으로 전송
int64_t result = publication_->offer(atomic_buffer, 0, length);

// 3. 결과 처리
if (result > 0) {
    // 성공 - result는 stream position
} else if (result == aeron::BACK_PRESSURED) {
    // Back pressure - 재시도 필요
}
```

#### Recording 제어 흐름

```cpp
// Recording 시작
subscription_id_ = archive_->startRecording(
    channel_,
    stream_id_,
    AeronArchive::SourceLocation::LOCAL
);

// Recording ID 조회
archive_->listRecordingsForUri(
    fromRecordingId,
    recordCount,
    channel,
    streamId,
    recordingDescriptorConsumer  // 콜백
);

// Recording 중지
archive_->stopRecording(subscription_id_);
```

---

### 2. Subscriber 구현

#### 주요 클래스

**AeronSubscriber**
- 역할: Live/Replay 모드 관리
- 주요 메서드:
  - `initialize()`: Aeron 및 Archive 연결
  - `startLive()`: Live 모드 시작
  - `startReplay(position)`: Replay 모드 시작
  - `run()`: 메시지 수신 루프

**ReplayToLiveHandler**
- 역할: Replay와 Live 전환 관리
- 주요 메서드:
  - `startReplay()`: Recording 조회 및 Replay 시작
  - `checkTransitionToLive()`: Replay 완료 감지 및 Live 전환
  - `poll()`: 메시지 수신

#### Subscription 모드

```cpp
enum class SubscriptionMode {
    REPLAY,         // Replay 중
    TRANSITIONING,  // 전환 중
    LIVE            // Live 스트림 수신
};
```

#### Replay 시작 흐름

```cpp
// 1. Recording ID 찾기
archive_->listRecordingsForUri(
    0, 10, channel, streamId,
    [&](int64_t recordingId, int64_t stopPos, ...) {
        recording_id = recordingId;
        stop_position = stopPos;
    }
);

// 2. Replay Subscription 생성
replay_subscription_ = aeron_->addSubscription(
    REPLAY_CHANNEL, REPLAY_STREAM_ID);

// 3. Replay 시작
replay_session_id_ = archive_->startReplay(
    recording_id,
    startPosition,
    length,
    REPLAY_CHANNEL,
    REPLAY_STREAM_ID
);

// 4. Live Subscription 미리 생성 (전환 준비)
live_subscription_ = aeron_->addSubscription(
    LIVE_CHANNEL, LIVE_STREAM_ID);
```

#### Replay-to-Live 전환 로직

```cpp
bool checkTransitionToLive() {
    if (mode_ != SubscriptionMode::REPLAY) {
        return false;
    }
    
    // Replay 완료 확인 (image 개수 = 0)
    if (replay_subscription_->imageCount() == 0) {
        mode_ = SubscriptionMode::TRANSITIONING;
        
        // Replay subscription 종료
        replay_subscription_.reset();
        
        // Live 모드로 전환
        mode_ = SubscriptionMode::LIVE;
        return true;
    }
    
    return false;
}
```

#### 메시지 수신 흐름

```cpp
int poll(MessageHandler handler, int fragmentLimit) {
    int fragments_read = 0;
    
    if (mode_ == REPLAY && replay_subscription_) {
        // Replay 모드: replay subscription에서 수신
        fragments_read = replay_subscription_->poll(
            fragment_handler, fragmentLimit);
        
        // Replay 완료 체크
        if (fragments_read == 0) {
            checkTransitionToLive();
        }
        
    } else if (mode_ == LIVE && live_subscription_) {
        // Live 모드: live subscription에서 수신
        fragments_read = live_subscription_->poll(
            fragment_handler, fragmentLimit);
    }
    
    return fragments_read;
}
```

---

### 3. ArchivingMediaDriver 설정

#### Java 실행 명령

```bash
java -cp aeron-all-1.50.0-SNAPSHOT.jar \
  -Daeron.dir=/dev/shm/aeron \
  -Daeron.archive.dir=/home/hesed/aeron-archive \
  -Daeron.archive.control.channel=aeron:udp?endpoint=localhost:8010 \
  -Daeron.archive.recording.events.channel=aeron:udp?endpoint=localhost:8011 \
  -Daeron.archive.replication.channel=aeron:udp?endpoint=localhost:8012 \
  -Daeron.threading.mode=SHARED \
  -Daeron.archive.threading.mode=SHARED \
  io.aeron.archive.ArchivingMediaDriver
```

#### 주요 파라미터 설명

| 파라미터 | 설명 |
|----------|------|
| `aeron.dir` | Aeron 공유 메모리 디렉토리 |
| `aeron.archive.dir` | Recording 파일 저장 디렉토리 |
| `aeron.archive.control.channel` | Archive 제어 채널 (C++ 클라이언트 접속용) |
| `aeron.archive.recording.events.channel` | Recording 이벤트 알림 채널 |
| `aeron.archive.replication.channel` | Archive 복제 채널 (필수 설정) |
| `aeron.threading.mode` | 스레딩 모드 (SHARED/DEDICATED) |

---

## 빌드 및 설치

### 1. 사전 요구사항

```bash
# 필수 패키지 설치
sudo yum install -y gcc-c++ cmake make git java-17-openjdk

# Aeron 빌드 확인
ls /home/hesed/aeron/include/
ls /home/hesed/aeron/lib/
```

### 2. 프로젝트 빌드

```bash
cd /home/hesed/devel/aeron

# 빌드 디렉토리 생성
mkdir -p build
cd build

# CMake 설정
cmake ..

# 컴파일
make -j$(nproc)

# 실행 파일 확인
ls -lh publisher/aeron_publisher
ls -lh subscriber/aeron_subscriber
```

### 3. 빌드 출력

성공 시:
```
[ 16%] Building CXX object common/CMakeFiles/aeron_common.dir/src/Logger.cpp.o
[ 33%] Building CXX object common/CMakeFiles/aeron_common.dir/src/AeronConfig.cpp.o
[ 50%] Linking CXX static library libaeron_common.a
[ 50%] Built target aeron_common
[ 66%] Building CXX object publisher/CMakeFiles/aeron_publisher.dir/src/AeronPublisher.cpp.o
[ 83%] Building CXX object publisher/CMakeFiles/aeron_publisher.dir/src/RecordingController.cpp.o
[100%] Linking CXX executable aeron_publisher
[100%] Built target aeron_publisher
[100%] Built target aeron_subscriber
```

---

## 실행 방법

### 1. 환경 준비

```bash
# Archive 디렉토리 생성
mkdir -p /home/hesed/aeron-archive

# Aeron 공유 메모리 정리 (선택사항)
rm -rf /dev/shm/aeron/*
```

### 2. ArchivingMediaDriver 시작

**Terminal 1:**

```bash
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh
```

**예상 출력:**
```
==========================================
Starting Aeron ArchivingMediaDriver
==========================================
Aeron Directory: /dev/shm/aeron
Archive Directory: /home/hesed/aeron-archive
Control Channel: aeron:udp?endpoint=localhost:8010
Recording Events: aeron:udp?endpoint=localhost:8011
Replication Channel: aeron:udp?endpoint=localhost:8012

(프로세스가 계속 실행됨)
```

### 3. Publisher 시작

**Terminal 2:**

```bash
cd /home/hesed/devel/aeron/build
./publisher/aeron_publisher
```

**예상 출력:**
```
Initializing Publisher...
Connected to Aeron
Publication added with registration ID: 23
Publication ready: aeron:udp?endpoint=localhost:40456, streamId: 10
Connected to Archive
Publisher initialized successfully
Publisher running. Type 'start' to begin recording, 'stop' to end recording, 'quit' to exit.
Published 1000 messages. Recording: OFF
Published 2000 messages. Recording: OFF
```

### 4. Subscriber 시작

#### Live 모드

**Terminal 3:**

```bash
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber
```

**예상 출력:**
```
Initializing Subscriber...
Connected to Aeron
Connected to Archive
Subscriber initialized successfully
Starting in LIVE mode...
Starting live subscription
Live subscription started
Subscriber running. Press Ctrl+C to exit.
[LIVE] Received message #100 at position 12800: Message 95 at 1699459200000000000
[LIVE] Received message #200 at position 25600: Message 195 at 1699459210000000000
```

#### Replay 모드

```bash
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber --replay 0
```

**예상 출력:**
```
Initializing Subscriber...
Connected to Aeron
Connected to Archive
Subscriber initialized successfully
Starting in REPLAY mode from position: 0
Starting replay from position: 0
Found recording ID: 1, stopPosition: 51200
Replay subscription created
Replay started. Session ID: 67890
Live subscription pre-created
Subscriber running. Press Ctrl+C to exit.
[REPLAY] Received message #100 at position 12800: Message 95 at ...
[REPLAY] Received message #200 at position 25600: Message 195 at ...
...
Replay completed. Transitioning to live...
Transitioned to live mode
[LIVE] Received message #300 at position 38400: Message 295 at ...
```

### 5. Publisher 명령어

Publisher 실행 중 다음 명령어 입력 가능:

| 명령어 | 설명 |
|--------|------|
| `start` | Recording 시작 |
| `stop` | Recording 중지 |
| `quit` | Publisher 종료 |

**사용 예:**
```
> start
Starting recording on channel: aeron:udp?endpoint=localhost:40456, streamId: 10
Recording subscription created with ID: 12345
Found recording ID: 1
Recording started successfully. ID: 1
Published 3000 messages. Recording: ON

> stop
Stopping recording ID: 1
Recording stopped successfully
Published 4000 messages. Recording: OFF

> quit
Shutting down Publisher...
Publisher shutdown complete. Total messages: 4000
```

---

## 테스트

### 테스트 시나리오

#### 테스트 1: Live Streaming

**목적**: 실시간 메시지 전송 확인

**절차:**
1. ArchivingMediaDriver 시작
2. Publisher 시작
3. Subscriber (Live 모드) 시작
4. Publisher에서 메시지 발행 확인
5. Subscriber에서 메시지 수신 확인

**성공 조건:**
- Subscriber가 실시간으로 메시지 수신
- 메시지 손실 없음
- Position이 순차적으로 증가

---

#### 테스트 2: Recording 제어

**목적**: Recording 시작/중지 동작 확인

**절차:**
1. ArchivingMediaDriver 시작
2. Publisher 시작
3. Publisher에서 `start` 명령 입력
4. "Recording started" 메시지 확인
5. 10초 대기 (약 100개 메시지 녹화)
6. Publisher에서 `stop` 명령 입력
7. "Recording stopped" 메시지 확인
8. Archive 디렉토리 확인

```bash
ls -lh /home/hesed/aeron-archive/
```

**성공 조건:**
- Recording ID 생성 확인
- Archive 디렉토리에 recording 파일 생성
- Recording 중 "Recording: ON" 표시
- Recording 중지 후 "Recording: OFF" 표시

**예상 출력:**
```
/home/hesed/aeron-archive/
├── archive-catalog.dat
└── 1-0-123456789.rec
```

---

#### 테스트 3: Replay

**목적**: 녹화된 데이터 재생 확인

**절차:**
1. 테스트 2 완료 (Recording 데이터 존재)
2. Subscriber (Live 모드) 종료
3. Publisher는 계속 실행 (새 메시지 발행 중)
4. Subscriber (Replay 모드) 시작:
   ```bash
   ./subscriber/aeron_subscriber --replay 0
   ```
5. Replay 메시지 수신 확인
6. Replay 완료 후 Live 전환 확인

**성공 조건:**
- Replay 모드에서 녹화된 메시지 수신
- "[REPLAY]" 태그로 메시지 표시
- Replay 완료 후 자동으로 "[LIVE]" 모드 전환
- Live 전환 후 실시간 메시지 수신

**예상 출력:**
```
[REPLAY] Received message #100 at position 12800: Message 95 at ...
[REPLAY] Received message #200 at position 25600: Message 195 at ...
Replay completed. Transitioning to live...
Transitioned to live mode
[LIVE] Received message #300 at position 38400: Message 295 at ...
```

---

#### 테스트 4: Replay-to-Live 전환 (종합)

**목적**: Replay에서 Live로의 seamless 전환 확인

**절차:**
1. ArchivingMediaDriver 시작
2. Publisher 시작
3. Recording 시작 (`start`)
4. 10초 대기
5. Recording 중지 (`stop`)
6. Subscriber (Replay 모드, position=0) 시작
7. Replay 진행 관찰
8. Replay 완료 및 Live 전환 관찰
9. Live 메시지 수신 확인

**성공 조건:**
- Replay 데이터 모두 수신
- Replay → Live 전환 시 메시지 누락 없음
- 모드 전환 메시지 출력
- Live 모드에서 실시간 메시지 계속 수신

---

### 테스트 체크리스트

- [ ] ArchivingMediaDriver 정상 시작
- [ ] Publisher 정상 시작 및 메시지 발행
- [ ] Subscriber Live 모드에서 메시지 수신
- [ ] Recording 시작 명령 동작
- [ ] Recording 중지 명령 동작
- [ ] Archive 디렉토리에 recording 파일 생성
- [ ] Replay 모드에서 녹화된 메시지 재생
- [ ] Replay 완료 감지
- [ ] Replay → Live 자동 전환
- [ ] Live 모드에서 실시간 메시지 계속 수신

---

## 트러블슈팅

### 문제 1: "Failed to connect to Archive"

**증상:**
```
Failed to initialize Publisher: Unable to connect to Archive
```

**원인:**
- ArchivingMediaDriver가 실행되지 않음
- Archive Control 채널 불일치

**해결:**
```bash
# 1. ArchivingMediaDriver 프로세스 확인
ps aux | grep ArchivingMediaDriver

# 2. Aeron 디렉토리 확인
ls -la /dev/shm/aeron/

# 3. ArchivingMediaDriver 재시작
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh
```

---

### 문제 2: "Publication not connected"

**증상:**
```
Publication added with registration ID: 23
(Publication이 연결되지 않음)
```

**원인:**
- UDP 채널 설정 오류
- Subscriber가 실행되지 않음

**해결:**
```bash
# 1. 채널 설정 확인
# AeronConfig.h의 PUBLICATION_CHANNEL과 SUBSCRIPTION_CHANNEL이 동일한지 확인

# 2. Subscriber 시작
./subscriber/aeron_subscriber

# 3. 멀티캐스트 사용 시 라우트 확인
route -n | grep 224
```

---

### 문제 3: "No recording found"

**증상:**
```
No recording found for channel: aeron:udp?endpoint=localhost:40456, streamId: 10
```

**원인:**
- Recording이 생성되지 않음
- Recording이 아직 완료되지 않음

**해결:**
```bash
# 1. Archive 디렉토리 확인
ls -la /home/hesed/aeron-archive/

# 2. Publisher에서 Recording 시작 확인
# 'start' 명령 후 "Recording started successfully" 메시지 확인

# 3. Recording 데이터 생성 대기
# 최소 몇 초간 메시지 발행 필요
```

---

### 문제 4: "Replay가 시작되지 않음"

**증상:**
```
Starting replay from position: 0
(메시지가 수신되지 않음)
```

**원인:**
- Recording ID를 찾지 못함
- Position이 잘못됨

**해결:**
```bash
# 1. Recording 파일 확인
ls -lh /home/hesed/aeron-archive/*.rec

# 2. Position 0부터 시작
./subscriber/aeron_subscriber --replay 0

# 3. ArchivingMediaDriver 로그 확인
# (Terminal 1에서 에러 메시지 확인)
```

---

### 문제 5: "Replay → Live 전환이 안 됨"

**증상:**
```
[REPLAY] Received message #200...
(계속 Replay 모드 유지)
```

**원인:**
- Replay가 완료되지 않음
- Live subscription이 생성되지 않음

**해결:**
```cpp
// checkTransitionToLive() 로직 확인
// imageCount() == 0 조건 체크

// 디버그 로그 추가
std::cout << "Image count: " << replay_subscription_->imageCount() << std::endl;
```

---

### 문제 6: 빌드 오류

**증상:**
```
fatal error: client/AeronArchive.h: No such file or directory
```

**원인:**
- Aeron include 경로 설정 오류

**해결:**
```bash
# 1. Aeron 헤더 경로 확인
ls /home/hesed/aeron/include/client/AeronArchive.h

# 2. CMakeLists.txt의 include 경로 확인
cat /home/hesed/devel/aeron/CMakeLists.txt | grep include_directories

# 3. 재빌드
cd /home/hesed/devel/aeron/build
rm -rf *
cmake ..
make -j$(nproc)
```

---

## API 참조

### Publisher API

#### AeronPublisher 클래스

```cpp
class AeronPublisher {
public:
    AeronPublisher();
    ~AeronPublisher();
    
    // 초기화
    bool initialize();
    
    // 메시지 발행
    bool publish(const uint8_t* buffer, size_t length);
    
    // Recording 제어
    bool startRecording();
    bool stopRecording();
    bool isRecording() const;
    
    // 실행
    void run();
    
    // 종료
    void shutdown();
};
```

**사용 예:**
```cpp
aeron::example::AeronPublisher publisher;

if (!publisher.initialize()) {
    return 1;
}

publisher.run();  // 블로킹 호출
```

---

#### RecordingController 클래스

```cpp
class RecordingController {
public:
    RecordingController(
        std::shared_ptr<aeron::archive::client::AeronArchive> archive,
        const std::string& channel,
        int streamId);
    
    ~RecordingController();
    
    bool startRecording();
    bool stopRecording();
    bool isRecording() const;
    int64_t getRecordingId() const;
};
```

**사용 예:**
```cpp
auto controller = std::make_unique<RecordingController>(
    archive, channel, streamId);

if (controller->startRecording()) {
    std::cout << "Recording ID: " << controller->getRecordingId() << std::endl;
}

// ...

controller->stopRecording();
```

---

### Subscriber API

#### AeronSubscriber 클래스

```cpp
class AeronSubscriber {
public:
    AeronSubscriber();
    ~AeronSubscriber();
    
    // 초기화
    bool initialize();
    
    // Live 모드 시작
    bool startLive();
    
    // Replay 모드 시작
    bool startReplay(int64_t startPosition);
    
    // 실행
    void run();
    
    // 종료
    void shutdown();
};
```

**사용 예 (Live):**
```cpp
aeron::example::AeronSubscriber subscriber;

if (!subscriber.initialize()) {
    return 1;
}

subscriber.startLive();
subscriber.run();  // 블로킹 호출
```

**사용 예 (Replay):**
```cpp
aeron::example::AeronSubscriber subscriber;

if (!subscriber.initialize()) {
    return 1;
}

int64_t startPosition = 0;
subscriber.startReplay(startPosition);
subscriber.run();  // 블로킹 호출
```

---

#### ReplayToLiveHandler 클래스

```cpp
enum class SubscriptionMode {
    REPLAY,
    TRANSITIONING,
    LIVE
};

class ReplayToLiveHandler {
public:
    using MessageHandler = std::function<void(
        const uint8_t* buffer, 
        size_t length, 
        int64_t position)>;
    
    ReplayToLiveHandler(
        std::shared_ptr<aeron::Aeron> aeron,
        std::shared_ptr<aeron::archive::client::AeronArchive> archive);
    
    ~ReplayToLiveHandler();
    
    bool startReplay(
        const std::string& channel,
        int streamId,
        int64_t startPosition);
    
    bool startLive(
        const std::string& channel,
        int streamId);
    
    int poll(MessageHandler handler, int fragmentLimit);
    
    SubscriptionMode getMode() const;
    
    void shutdown();
};
```

**사용 예:**
```cpp
auto handler = std::make_unique<ReplayToLiveHandler>(aeron, archive);

// Replay 시작
handler->startReplay(channel, streamId, 0);

// 메시지 수신 루프
while (running) {
    int fragments = handler->poll(
        [](const uint8_t* buffer, size_t length, int64_t position) {
            // 메시지 처리
            std::string msg(reinterpret_cast<const char*>(buffer), length);
            std::cout << "Received: " << msg << " at " << position << std::endl;
        },
        10  // fragmentLimit
    );
    
    if (fragments == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

---

### Aeron Archive C++ API

#### AeronArchive 연결

```cpp
// Context 생성
auto context = std::make_shared<aeron::archive::client::Context>();
context->aeron(aeron);
context->controlRequestChannel("aeron:udp?endpoint=localhost:8010");
context->controlResponseChannel("aeron:udp?endpoint=localhost:0");

// Archive 연결
auto archive = aeron::archive::client::AeronArchive::connect(*context);
```

#### Recording 시작

```cpp
int64_t subscriptionId = archive->startRecording(
    channel,
    streamId,
    aeron::archive::client::AeronArchive::SourceLocation::LOCAL
);
```

#### Recording 중지

```cpp
archive->stopRecording(subscriptionId);
```

#### Recording 목록 조회

```cpp
auto consumer = [](
    int64_t controlSessionId,
    int64_t correlationId,
    int64_t recordingId,
    int64_t startTimestamp,
    int64_t stopTimestamp,
    int64_t startPosition,
    int64_t stopPosition,
    int32_t initialTermId,
    int32_t segmentFileLength,
    int32_t termBufferLength,
    int32_t mtuLength,
    int32_t sessionId,
    int32_t streamId,
    const std::string& strippedChannel,
    const std::string& originalChannel,
    const std::string& sourceIdentity) {
    
    std::cout << "Recording ID: " << recordingId << std::endl;
};

int32_t count = archive->listRecordingsForUri(
    0,      // fromRecordingId
    10,     // recordCount
    channel,
    streamId,
    consumer
);
```

#### Replay 시작

```cpp
int64_t replaySessionId = archive->startReplay(
    recordingId,
    startPosition,
    length,
    replayChannel,
    replayStreamId
);
```

---

## 부록

### A. 편의 스크립트

#### run_test.sh

```bash
#!/bin/bash

ACTION=$1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="/home/hesed/devel/aeron/build"

case "$ACTION" in
    start-driver)
        ${SCRIPT_DIR}/start_archive_driver.sh
        ;;
    start-publisher)
        cd ${BUILD_DIR}
        ./publisher/aeron_publisher
        ;;
    start-subscriber)
        cd ${BUILD_DIR}
        ./subscriber/aeron_subscriber
        ;;
    start-replay)
        if [ -z "$2" ]; then
            echo "Usage: $0 start-replay <position>"
            exit 1
        fi
        cd ${BUILD_DIR}
        ./subscriber/aeron_subscriber --replay $2
        ;;
    clean)
        echo "Cleaning up..."
        rm -rf /dev/shm/aeron/*
        rm -rf /home/hesed/aeron-archive/*
        echo "Done"
        ;;
    status)
        echo "=========================================="
        echo "Aeron Status"
        echo "=========================================="
        echo ""
        echo "Java processes:"
        ps aux | grep -E "ArchivingMediaDriver|java.*aeron" | grep -v grep
        echo ""
        echo "C++ processes:"
        ps aux | grep -E "aeron_publisher|aeron_subscriber" | grep -v grep
        echo ""
        echo "Aeron directory:"
        ls -lh /dev/shm/aeron/ 2>/dev/null || echo "  (not found)"
        echo ""
        echo "Archive directory:"
        ls -lh /home/hesed/aeron-archive/ 2>/dev/null || echo "  (not found)"
        ;;
    *)
        echo "Usage: $0 {start-driver|start-publisher|start-subscriber|start-replay <pos>|clean|status}"
        echo ""
        echo "Examples:"
        echo "  $0 start-driver          # Terminal 1"
        echo "  $0 start-publisher       # Terminal 2"
        echo "  $0 start-subscriber      # Terminal 3"
        echo "  $0 start-replay 0        # Replay from position 0"
        echo "  $0 status                # Check status"
        echo "  $0 clean                 # Clean up all data"
        exit 1
        ;;
esac
```

**사용 예:**
```bash
# Terminal 1
./run_test.sh start-driver

# Terminal 2
./run_test.sh start-publisher

# Terminal 3
./run_test.sh start-subscriber

# Replay 테스트
./run_test.sh start-replay 0

# 상태 확인
./run_test.sh status

# 정리
./run_test.sh clean
```

---

### B. 성능 튜닝 가이드

#### 1. 메시지 처리량 향상

**Publisher 메시지 전송 간격 조정:**
```cpp
// AeronPublisher.cpp의 run() 함수에서
std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 100ms → 10ms
```

**ArchivingMediaDriver 메모리 설정:**
```bash
java -Xmx4g -Xms4g \  # 힙 메모리 증가
  -XX:+UseG1GC \
  -XX:MaxGCPauseMillis=10 \
  ...
```

#### 2. 지연시간 감소

**MTU 크기 증가:**
```bash
-Daeron.mtu.length=8192 \
-Daeron.ipc.mtu.length=8192
```

**버퍼 크기 증가:**
```bash
-Daeron.socket.so_sndbuf=2097152 \
-Daeron.socket.so_rcvbuf=2097152 \
-Daeron.rcv.initial.window.length=2097152
```

#### 3. Recording 성능

**파일 동기화 비활성화 (테스트 환경):**
```bash
-Daeron.archive.file.sync.level=0 \
-Daeron.archive.catalog.file.sync.level=0
```

**세그먼트 파일 크기 조정:**
```bash
-Daeron.archive.segment.file.length=134217728  # 128MB
```

---

### C. 참고 자료

- **Aeron 공식 문서**: https://github.com/real-logic/aeron/wiki
- **Aeron Archive 가이드**: https://github.com/real-logic/aeron/wiki/Archive-Cookbook
- **Aeron C++ API**: https://github.com/real-logic/aeron/tree/master/aeron-client
- **성능 튜닝**: https://github.com/real-logic/aeron/wiki/Configuration-Options

---

## 결론

본 프로젝트는 Aeron 메시징 시스템을 활용하여 고성능 Publisher/Subscriber 아키텍처를 구현하였습니다. 주요 성과는 다음과 같습니다:

### 구현 완료 항목
- ✅ C++ Wrapper API 기반 Publisher/Subscriber 구현
- ✅ Recording 시작/중지 제어 기능
- ✅ Position 기반 Replay 기능
- ✅ Replay-to-Live 자동 전환
- ✅ Java ArchivingMediaDriver 통합
- ✅ UDP 유니캐스트 전송

### 향후 개선 사항
- 멀티캐스트 네트워크 환경 구성
- 다중 Subscriber 지원 테스트
- 성능 벤치마크 및 최적화
- 에러 복구 메커니즘 강화
- 모니터링 ë¡깅 시스템 추가

---

**문서 버전**: 1.0  
**작성일**: 2024-11-09  
**최종 수정**: 2024-11-09  

---

## 라이선스

본 프로젝트는 Aeron (Apache License 2.0) 기반으로 개발되었습니다.

## 기여

이슈 및 개선 제안은 프로젝트 관리자에게 문의하시기 바랍니다.

## 연락처

**프로젝트 관리자**: Aeron Development Team  
**이메일**: [your-email@example.com]  
**프로젝트 위치**: `/home/hesed/devel/aeron`

