# 수작업 테스트 가이드

## 준비 단계

### 1. 터미널 3개 준비
- Terminal 1: ArchivingMediaDriver
- Terminal 2: Publisher
- Terminal 3: Subscriber

---

## 테스트 시나리오 1: 기본 Live 모드

### Terminal 1: ArchivingMediaDriver 시작

```bash
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh
```

**확인사항**:
- "ArchivingMediaDriver started" 메시지 출력
- 에러 없이 실행 중

**종료**: `Ctrl+C`

---

### Terminal 2: Test Publisher 시작

```bash
cd /home/hesed/devel/aeron/build

# 기본 테스트: 100ms 간격으로 50개 메시지
./test_message_publisher 100 50
```

**파라미터**:
- 첫 번째 인자: 메시지 간격 (ms)
- 두 번째 인자: 메시지 개수

**예제**:
```bash
# 빠른 테스트: 50ms 간격, 10개
./test_message_publisher 50 10

# 느린 테스트: 1000ms(1초) 간격, 5개
./test_message_publisher 1000 5
```

**확인사항**:
```
========================================
  Test MessageBuffer Publisher
========================================
Interval: 100 ms
Count: 50 messages
========================================

Connected to Aeron
Publication added: aeron:udp?endpoint=localhost:40456, streamId: 10
Publication ready

Starting to publish messages...
```

**초기 상태**:
- Subscriber가 없으면: `Not connected, waiting for subscriber...` 반복
- 정상: Subscriber 연결 후 `Sent 10 messages (seq: X)` 출력

**종료**: 자동 종료 (메시지 개수만큼 전송 후) 또는 `Ctrl+C`

---

### Terminal 3: Zero-copy Subscriber 시작

```bash
cd /home/hesed/devel/aeron/build

# Live 모드
./subscriber/aeron_subscriber_zerocopy
```

**확인사항**:
```
==========================================
    ZERO-COPY SUBSCRIBER (3 Threads)
==========================================
Mode: LIVE
==========================================

Creating Buffer Pool...
BufferPool initialized: 1024 buffers, 4 MB
Creating Message Queue...
MessageQueue initialized: 4096 slots, 32 KB
Creating Monitoring Queue...
Starting Monitoring Thread...
Creating Message Worker...
MessageWorker created
Message handler registered
Starting Worker Thread...
✓ Worker thread started
Initializing Subscriber...
  Aeron dir: /home/hesed/shm/aeron
  NOTE: External MediaDriver (aeronmd) must be running
Worker thread running (TID: XXXXXX)
Connected to Aeron
Archive control channel: aeron:udp?endpoint=localhost:8010
Connected to Archive
Subscriber initialized successfully
Enabling Zero-Copy Mode...
Zero-copy mode ENABLED
  Buffer pool capacity: 1024
  Message queue capacity: 4095

Starting Live mode...
  Subscription channel: aeron:udp?endpoint=localhost:40456
  Stream ID: 10
Subscription added with ID: XX
Live subscription ready

==========================================
  ✓ All threads running
  • Subscriber Thread: Aeron reception
  • Worker Thread: Message processing
  • Monitoring Thread: Statistics
==========================================

Press Ctrl+C to stop...
```

**메시지 수신 확인**:
- Worker thread가 메시지를 처리하면 로그 출력
- Monitoring thread가 100개마다 통계 출력 (예정)

**종료**: `Ctrl+C`

---

## 테스트 시나리오 2: 순서 확인

### 방법 1: Subscriber 먼저 시작 (권장)

```bash
# Terminal 1
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh

# 5초 대기 (MediaDriver 초기화)

# Terminal 3 (Subscriber 먼저!)
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber_zerocopy

# 3초 대기 (Subscriber 초기화)

# Terminal 2
cd /home/hesed/devel/aeron/build
./test_message_publisher 100 30
```

**장점**: Publisher가 즉시 연결되어 "Not connected" 메시지 최소화

---

### 방법 2: Publisher 먼저 시작

```bash
# Terminal 1
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh

# Terminal 2 (Publisher 먼저)
cd /home/hesed/devel/aeron/build
./test_message_publisher 100 30

# Publisher가 "Not connected" 출력하는 동안...

# Terminal 3
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber_zerocopy
```

**특징**: Publisher가 재시도하면서 대기, Subscriber 시작 후 연결

---

## 테스트 시나리오 3: ReplayMerge 모드

### Terminal 1: ArchivingMediaDriver
```bash
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh
```

### Terminal 2: Publisher (Recording 활성화)
```bash
cd /home/hesed/devel/aeron/build

# 기존 aeron_publisher 사용 (interactive mode)
./publisher/aeron_publisher --config ../config/aeron-local.ini --interval 100

# 실행 후 명령 입력:
# start    <- recording 시작
# (메시지 전송됨)
# stop     <- recording 종료
# quit     <- 종료
```

### Terminal 3: Subscriber (ReplayMerge Auto 모드)
```bash
cd /home/hesed/devel/aeron/build

# ReplayMerge Auto 모드
./subscriber/aeron_subscriber_zerocopy --replay-auto
```

**확인사항**:
- Replay 메시지 수신: `[REPLAY]` 태그
- Live 메시지 수신: `[LIVE]` 태그
- 자동 전환 확인

---

## 디버깅 팁

### 1. 프로세스 확인
```bash
# ArchivingMediaDriver 실행 중?
pgrep -f ArchivingMediaDriver

# Publisher 실행 중?
ps aux | grep test_message_publisher | grep -v grep

# Subscriber 실행 중?
ps aux | grep aeron_subscriber | grep -v grep
```

### 2. 프로세스 종료
```bash
# 모든 Publisher 종료
pkill -f test_message_publisher
pkill -f aeron_publisher

# 모든 Subscriber 종료
pkill -f aeron_subscriber

# ArchivingMediaDriver 종료 (주의!)
pkill -f ArchivingMediaDriver
```

### 3. Aeron 디렉토리 확인
```bash
# 공유 메모리 상태
ls -lh /home/hesed/shm/aeron/

# cnc.dat 파일 확인 (MediaDriver 실행 중이면 존재)
ls -lh /home/hesed/shm/aeron/cnc.dat

# Archive 데이터
ls -lh /home/hesed/shm/aeron-archive/
```

### 4. 로그 확인
```bash
# Archive driver 로그
tail -f /home/hesed/devel/aeron/logs/archive_driver.log

# 최근 에러 로그
ls -lt /home/hesed/shm/aeron-archive/*error.log | head -1
```

---

## 체크리스트

### 테스트 전
- [ ] ArchivingMediaDriver 실행 중 확인
- [ ] `/home/hesed/shm/aeron/` 디렉토리 존재 확인
- [ ] 이전 테스트 프로세스 종료 확인

### 테스트 중
- [ ] Publisher 연결 성공 확인
- [ ] Subscriber 초기화 성공 확인
- [ ] 메시지 수신 확인
- [ ] 에러 메시지 없는지 확인

### 테스트 후
- [ ] 모든 프로세스 정상 종료
- [ ] 백그라운드 프로세스 없는지 확인
- [ ] 로그에 에러 없는지 확인

---

## 예상 결과

### 성공적인 테스트
```
Publisher:
  Sent 10 messages (seq: 9)
  Sent 20 messages (seq: 19)
  Sent 30 messages (seq: 29)
  Publishing complete!
  Total sent: 30 messages

Subscriber:
  ✓ All threads running
  Worker thread processing messages...
  (메시지 처리 로그)
  Monitoring stats: 30 messages received
```

### 실패 케이스

**케이스 1: MediaDriver 미실행**
```
Failed to connect to Aeron: no driver heartbeat detected
```
**해결**: Terminal 1에서 ArchivingMediaDriver 시작

**케이스 2: 연결 타임아웃**
```
Not connected, waiting for subscriber... (계속 반복)
```
**해결**: Subscriber가 실행 중인지 확인, 3-5초 대기

**케이스 3: 메시지 수신 없음**
```
[LIVE] Received 0 messages (계속 반복)
```
**해결**: Publisher가 메시지를 보내는지 확인, 포트/채널 설정 확인

---

## 간단 테스트 스크립트

다음 세션용 빠른 테스트:

```bash
#!/bin/bash
# quick_test.sh

echo "1. Starting ArchivingMediaDriver..."
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh &
sleep 5

echo "2. Starting Subscriber..."
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber_zerocopy &
SUBSCRIBER_PID=$!
sleep 3

echo "3. Starting Publisher..."
./test_message_publisher 100 20

echo "4. Waiting for completion..."
sleep 3

echo "5. Stopping Subscriber..."
kill $SUBSCRIBER_PID

echo "Done!"
```

**실행**:
```bash
chmod +x quick_test.sh
./quick_test.sh
```

---

**작성일**: 2025-11-19
**다음 업데이트**: 통합 테스트 완료 후 실제 결과 추가
