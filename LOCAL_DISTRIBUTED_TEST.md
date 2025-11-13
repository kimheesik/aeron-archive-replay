# 로컬 서버에서 분산 환경 재현 가이드

**작성일**: 2025-11-13
**목적**: 단일 서버에서 Publisher 1개 + Subscriber 여러 개 분산 환경 시뮬레이션

---

## 개요

실제 분산 환경이 없어도 **로컬 서버에서** Publisher와 여러 Subscriber가 마치 다른 서버에 있는 것처럼 동작하도록 테스트할 수 있습니다.

### 시뮬레이션 구조

```
단일 서버 (localhost)
├─ Terminal 1: ArchivingMediaDriver (백그라운드)
│  ├─ Aeron Dir: /home/hesed/shm/aeron
│  └─ Archive Dir: /home/hesed/shmaeron
│
├─ Terminal 2: Publisher
│  └─ Aeron Dir: /home/hesed/shm/aeron (shared with Driver)
│
├─ Terminal 3: Subscriber #1 (Embedded Driver)
│  └─ Aeron Dir: /dev/shm/aeron-sub1
│
└─ Terminal 4: Subscriber #2 (Embedded Driver)
   └─ Aeron Dir: /dev/shm/aeron-sub2

통신: Multicast 224.0.1.1:40456 (localhost loopback)
```

**주요 변경사항**:
- Aeron Dir: `/home/hesed/shm/aeron` (일반 파일시스템, 기존 설정과 호환)
- Archive Dir: `/home/hesed/shmaeron` (별도 디렉토리)
- ArchivingMediaDriver: **백그라운드 실행** (`&` 추가)

### 핵심 포인트

✅ **다른 Aeron 디렉토리** - 각 프로세스가 독립적으로 동작
✅ **Multicast 사용** - 1:N 통신 재현
✅ **Embedded MediaDriver** - Subscriber는 Java 프로세스 불필요
✅ **단일 Archive** - Publisher 서버의 Archive만 사용

---

## 사전 준비

### 1. Multicast Loopback 활성화

```bash
# Multicast route 확인
ip route show | grep 224

# 없으면 추가 (loopback)
sudo ip route add 224.0.0.0/4 dev lo

# 확인
ping -c 3 224.0.1.1
```

### 2. 디렉토리 생성

```bash
# Aeron 디렉토리들 (자동 생성되지만 수동으로 생성해도 됨)
mkdir -p /home/hesed/shm/aeron          # ArchivingMediaDriver & Publisher
mkdir -p /dev/shm/aeron-sub1            # Subscriber #1
mkdir -p /dev/shm/aeron-sub2            # Subscriber #2

# Archive 디렉토리
mkdir -p /home/hesed/shmaeron
```

---

## Config 파일

### Publisher용: `config/local-distributed-publisher.ini`

```ini
[aeron]
# ArchivingMediaDriver와 공유하는 디렉토리
dir = /home/hesed/shm/aeron

[archive]
control_request_channel = aeron:udp?endpoint=localhost:8010
control_response_channel = aeron:udp?endpoint=localhost:0

[publication]
channel = aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1
stream_id = 10

[subscription]
channel = aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1
stream_id = 10

[replay]
channel = aeron:udp?endpoint=localhost:40457
stream_id = 20

[timeouts]
idle_sleep_ms = 1
message_timeout_ns = 10000000000
```

### Subscriber용: `config/local-distributed-subscriber.ini`

```ini
[aeron]
dir = /dev/shm/aeron-subscriber

[archive]
control_request_channel = aeron:udp?endpoint=localhost:8010
control_response_channel = aeron:udp?endpoint=localhost:0

[publication]
channel = aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1
stream_id = 10

[subscription]
channel = aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1
stream_id = 10

[replay]
channel = aeron:udp?endpoint=localhost:40457
stream_id = 20

[timeouts]
idle_sleep_ms = 1
message_timeout_ns = 10000000000
```

---

## 실행 방법

### 🖥️ Terminal 1: ArchivingMediaDriver (백그라운드)

```bash
cd /home/hesed/devel/aeron

# 로컬 분산 시뮬레이션용 스크립트 (백그라운드 실행)
./scripts/start_archive_driver_local_distributed.sh

# 로그 확인 (실행 후)
tail -f logs/archive_driver_local_distributed.log
```

**출력 확인**:
```
Starting ArchivingMediaDriver (Local Distributed Simulation)
Aeron Directory: /home/hesed/shm/aeron
Archive Directory: /home/hesed/shmaeron
Control Channel: aeron:udp?endpoint=localhost:8010
```

**주의**: 스크립트가 백그라운드로 실행되므로 터미널이 즉시 반환됩니다.

### 🖥️ Terminal 2: Publisher

```bash
cd /home/hesed/devel/aeron/build

./publisher/aeron_publisher \
  --config ../config/local-distributed-publisher.ini
```

**대화형 명령**:
- `start` - Recording 시작
- `stop` - Recording 중지
- `quit` - 종료

### 🖥️ Terminal 3: Subscriber #1 (Embedded Driver)

```bash
cd /home/hesed/devel/aeron/build

./subscriber/aeron_subscriber \
  --config ../config/local-distributed-subscriber.ini \
  --embedded \
  --aeron-dir /dev/shm/aeron-sub1
```

**출력 확인**:
```
========================================
Subscriber Configuration
========================================
Aeron directory: /dev/shm/aeron-sub1
Embedded driver: YES
Archive control: aeron:udp?endpoint=localhost:8010
Subscription channel: aeron:udp?endpoint=224.0.1.1:40456|interface=127.0.0.1
Mode: LIVE
========================================
```

### 🖥️ Terminal 4: Subscriber #2 (Embedded Driver)

```bash
cd /home/hesed/devel/aeron/build

./subscriber/aeron_subscriber \
  --config ../config/local-distributed-subscriber.ini \
  --embedded \
  --aeron-dir /dev/shm/aeron-sub2
```

**핵심**: `--aeron-dir`를 **다르게** 지정하여 독립적인 MediaDriver 사용

---

## 테스트 시나리오

### 시나리오 1: 기본 Multicast 테스트

**목적**: Publisher 메시지가 모든 Subscriber에게 전달되는지 확인

1. 모든 터미널 실행 (1~4)
2. Publisher에서 `start` 입력
3. Subscriber #1, #2에서 **동일한 메시지** 수신 확인
4. Publisher에서 `stop` 입력

**예상 결과**:
- Subscriber #1, #2 모두 동일한 메시지 번호 수신
- 레이턴시 통계 출력 (100개 메시지마다)

### 시나리오 2: Subscriber 동적 추가

**목적**: 메시지 전송 중에 새 Subscriber 추가

1. Terminal 1, 2, 3만 실행 (Subscriber #1만)
2. Publisher에서 `start` 입력
3. Subscriber #1이 메시지 수신 중
4. **Terminal 4 (Subscriber #2) 시작**
5. Subscriber #2도 메시지 수신 시작

**예상 결과**:
- Subscriber #2는 시작 시점부터 메시지 수신
- 이전 메시지는 못 받음 (Replay 없이는)

### 시나리오 3: Replay 테스트

**목적**: 과거 메시지 재생

1. Publisher에서 메시지 전송 후 `stop`
2. Subscriber #1, #2 종료
3. **새 Subscriber (Replay 모드)** 시작:

```bash
./subscriber/aeron_subscriber \
  --config ../config/local-distributed-subscriber.ini \
  --embedded \
  --aeron-dir /dev/shm/aeron-sub3 \
  --replay 0
```

**예상 결과**:
- 처음부터 녹화된 메시지 재생
- `[REPLAY]` 태그 출력 (구현된 경우)
- Replay 완료 후 Live 모드 전환

### 시나리오 4: 여러 Subscriber 동시 수신

**목적**: N개의 Subscriber가 동시에 메시지 수신

1. Terminal 1, 2 실행 (ArchivingMediaDriver + Publisher)
2. Publisher에서 `start`
3. **여러 터미널에서 Subscriber 실행**:

```bash
# Terminal 3
./subscriber/aeron_subscriber --config ../config/local-distributed-subscriber.ini \
  --embedded --aeron-dir /dev/shm/aeron-sub1

# Terminal 4
./subscriber/aeron_subscriber --config ../config/local-distributed-subscriber.ini \
  --embedded --aeron-dir /dev/shm/aeron-sub2

# Terminal 5
./subscriber/aeron_subscriber --config ../config/local-distributed-subscriber.ini \
  --embedded --aeron-dir /dev/shm/aeron-sub3
```

**예상 결과**:
- 모든 Subscriber가 **동시에** 동일한 메시지 수신
- 각 Subscriber의 레이턴시 통계 독립적

---

## 성능 측정

### 레이턴시 확인

Subscriber 출력에서 레이턴시 통계 확인:

```
========================================
Latency Statistics (100 samples)
========================================
Min:     50.00 μs
Max:     500.00 μs
Average: 150.00 μs
========================================
```

### 메시지 처리율

Publisher에서 전송 간격 조정:

```bash
# 10ms 간격 (초당 100개 메시지)
./publisher/aeron_publisher \
  --config ../config/local-distributed-publisher.ini \
  --interval 10

# 1ms 간격 (초당 1000개 메시지)
./publisher/aeron_publisher \
  --config ../config/local-distributed-publisher.ini \
  --interval 1
```

---

## 트러블슈팅

### 문제 1: "Multicast 메시지 수신 안 됨"

**원인**: Multicast route 미설정

**해결**:
```bash
# Multicast route 추가
sudo ip route add 224.0.0.0/4 dev lo

# 확인
ip route show | grep 224
```

### 문제 2: "Aeron 디렉토리 충돌"

**현상**:
```
ERROR - CnC file already exists
```

**원인**: 동일한 aeron-dir 사용

**해결**:
```bash
# 각 Subscriber에 다른 aeron-dir 지정
--aeron-dir /dev/shm/aeron-sub1
--aeron-dir /dev/shm/aeron-sub2
--aeron-dir /dev/shm/aeron-sub3
```

### 문제 3: "Embedded MediaDriver 시작 실패"

**현상**:
```
Failed to fork MediaDriver process
```

**해결**:
```bash
# Java 설치 확인
java -version

# Aeron JAR 경로 확인
ls -la /home/hesed/aeron/bin/aeron-all-1.50.0-SNAPSHOT.jar

# AeronSubscriber.cpp:58 경로 수정 (필요 시)
```

### 문제 4: "Archive 연결 실패"

**현상**:
```
Failed to connect to Archive
```

**해결**:
```bash
# ArchivingMediaDriver 실행 확인
ps aux | grep ArchivingMediaDriver

# 포트 확인
netstat -tuln | grep 8010

# 로그 확인
tail -f logs/archive_driver_local_distributed.log
```

---

## 정리 (Cleanup)

테스트 후 정리:

```bash
# ArchivingMediaDriver 종료 (백그라운드 프로세스)
pkill -f "io.aeron.archive.ArchivingMediaDriver"

# 또는 프로세스 ID로 종료
ps aux | grep ArchivingMediaDriver
kill <PID>

# Publisher/Subscriber 종료 (Ctrl+C)

# Aeron 디렉토리 정리
rm -rf /home/hesed/shm/aeron
rm -rf /dev/shm/aeron-sub1
rm -rf /dev/shm/aeron-sub2
rm -rf /dev/shm/aeron-sub3

# Archive 정리 (선택사항 - Recording 삭제)
rm -rf /home/hesed/shmaeron

# MediaDriver 프로세스 강제 종료 (필요 시)
pkill -9 -f "io.aeron.driver.MediaDriver"
pkill -9 -f "io.aeron.archive.ArchivingMediaDriver"
```

---

## 실제 분산 환경과의 차이점

| 항목 | 로컬 시뮬레이션 | 실제 분산 환경 |
|------|----------------|----------------|
| **네트워크** | Loopback (127.0.0.1) | 실제 네트워크 인터페이스 |
| **Multicast** | 224.0.1.1 via lo | 224.0.1.1 via eth0 |
| **Aeron Dir** | /dev/shm/aeron-* (다름) | 각 서버 /dev/shm/aeron (같음) |
| **Archive** | 단일 서버 | Publisher 서버 |
| **레이턴시** | 매우 낮음 (~50μs) | 네트워크 latency 추가 |

---

## 고급 테스트

### 메시지 손실 테스트

```bash
# Publisher를 빠른 간격으로 실행
./publisher/aeron_publisher \
  --config ../config/local-distributed-publisher.ini \
  --interval 1

# Subscriber의 메시지 카운터 확인
# 순차적으로 증가하는지 확인
```

### Subscriber 장애 복구

1. Subscriber #1 실행
2. Publisher 메시지 전송 중
3. Subscriber #1 종료 (Ctrl+C)
4. Subscriber #1 재시작
5. 메시지 수신 재개 확인

### 네트워크 대역폭 테스트

```bash
# 큰 메시지 전송 (Publisher 코드 수정 필요)
# 또는 매우 짧은 간격
./publisher/aeron_publisher \
  --config ../config/local-distributed-publisher.ini \
  --interval 0  # 최대한 빠르게
```

---

## 요약

### 실행 순서 (Quick Start)

```bash
# 1. ArchivingMediaDriver
./scripts/start_archive_driver_local_distributed.sh

# 2. Publisher
./build/publisher/aeron_publisher --config config/local-distributed-publisher.ini

# 3. Subscriber #1
./build/subscriber/aeron_subscriber \
  --config config/local-distributed-subscriber.ini \
  --embedded --aeron-dir /dev/shm/aeron-sub1

# 4. Subscriber #2
./build/subscriber/aeron_subscriber \
  --config config/local-distributed-subscriber.ini \
  --embedded --aeron-dir /dev/shm/aeron-sub2
```

### 핵심 포인트

✅ **Multicast** - 224.0.1.1:40456 (loopback via 127.0.0.1)
✅ **다른 Aeron Dir** - 각 프로세스 독립적
✅ **Embedded Driver** - Subscriber는 Java 불필요
✅ **단일 Archive** - Publisher 서버만

**완료!** 로컬에서 분산 환경을 완벽히 재현할 수 있습니다! 🎉

---

## 변경 이력

### 2025-11-13 (업데이트)

**스크립트 변경** (`start_archive_driver_local_distributed.sh`):
- ✅ Aeron Dir: `/dev/shm/aeron-publisher` → `/home/hesed/shm/aeron`
  - 기존 설정과 호환성 유지
  - 일반 파일시스템 사용 (tmpfs 대신)
- ✅ Archive Dir: `/dev/shm/aeron-archive` → `/home/hesed/shmaeron`
  - 별도 디렉토리로 분리
- ✅ 백그라운드 실행: 명령 마지막에 `&` 추가
  - 터미널이 즉시 반환되어 다른 명령 실행 가능
  - 로그는 `logs/archive_driver_local_distributed.log`에 기록

**Config 파일 변경**:
- ✅ `config/local-distributed-publisher.ini`: Aeron dir 업데이트

**권장 사항**:
- Archive Dir 오타 수정 권장: `/home/hesed/shmaeron` → `/home/hesed/shm/aeron-archive`
- 또는 현재 경로 유지 (양쪽 모두 작동)
