# 빠른 시작 가이드 (Quick Start)

**최종 업데이트**: 2025-11-13
**목적**: 로컬에서 가장 빠르고 안정적으로 테스트

---

## 🚀 가장 간단한 실행 방법

### 전제 조건

```bash
# 빌드 완료 확인
cd /home/hesed/devel/aeron/build
ls -la publisher/aeron_publisher subscriber/aeron_subscriber
```

---

## 📋 3-Terminal 구성 (IPC - Shared Memory)

### Terminal 1: ArchivingMediaDriver

```bash
cd /home/hesed/devel/aeron

# 백그라운드 실행
./scripts/start_archive_driver_local_distributed.sh

# 확인
ps aux | grep ArchivingMediaDriver

# 로그 모니터링 (선택사항)
tail -f logs/archive_driver_local_distributed.log
```

### Terminal 2: Publisher

```bash
cd /home/hesed/devel/aeron/build

# 간단한 unicast 설정 사용
./publisher/aeron_publisher --config ../config/local-simple-publisher.ini

# 대화형 명령
start  # Recording 시작
stop   # Recording 중지
quit   # 종료
```

### Terminal 3: Subscriber

```bash
cd /home/hesed/devel/aeron/build

# ArchivingMediaDriver 공유 (embedded 불필요, IPC 사용)
./subscriber/aeron_subscriber \
  --config ../config/local-simple.ini \
  --aeron-dir /home/hesed/shm/aeron
```

**핵심**:
- **로컬 테스트**에서는 embedded driver 불필요!
- `--aeron-dir`를 **ArchivingMediaDriver와 동일하게** 지정 (IPC 통신)
- `aeron:ipc` 채널 사용 (가장 빠른 로컬 통신)

---

## ✅ 성공 확인

### Publisher 출력
```
Publisher initialized successfully
Publisher running. Type 'start' to begin recording
```

### Subscriber 출력
```
Subscriber initialized successfully
Starting in LIVE mode...
Live subscription started
Subscriber running. Press Ctrl+C to exit.
```

### 메시지 수신 확인

Publisher에서 `start` 입력 후, Subscriber에서:
```
Received message: Message 0 at 1699887654123456789
Received message: Message 1 at 1699887654223456789
...

========================================
Latency Statistics (100 samples)
========================================
Min:     45.23 μs
Max:     312.45 μs
Average: 128.67 μs
========================================
```

---

## 🔧 여러 Subscriber 실행

### Subscriber #2 추가

```bash
cd /home/hesed/devel/aeron/build

# IPC는 같은 aeron-dir 필요 (추가 설정 불필요)
./subscriber/aeron_subscriber \
  --config ../config/local-simple.ini \
  --aeron-dir /home/hesed/shm/aeron
```

**참고**: IPC 사용 시 모든 subscriber가 **같은 aeron-dir** 공유 (여러 subscriber 동시 실행 가능)

---

## ❌ 흔한 에러와 해결

### 에러 1: "active driver detected"

```
Exception: ActiveDriverException: ERROR - active driver detected
```

**원인**: --embedded 옵션 사용 시 같은 aeron-dir 충돌

**해결** (로컬 테스트):
```bash
# ❌ 잘못됨 (embedded 사용하면서 같은 dir)
--embedded --aeron-dir /home/hesed/shm/aeron

# ✅ 올바름 (embedded 없이 ArchivingMediaDriver 공유)
--aeron-dir /home/hesed/shm/aeron
```

**참고**: 분산 환경에서는 각 서버에서 `--embedded --aeron-dir /dev/shm/aeron-sub1` 사용

### 에러 2: "Unable to find multicast interface"

```
Unable to find multicast interface matching criteria
```

**원인**: Multicast 설정이 로컬에서 작동 안 함

**해결**: IPC config 사용 (local-simple.ini)
```bash
# IPC 사용 (가장 빠름, 로컬 전용)
--config ../config/local-simple.ini
# Config 내용: channel = aeron:ipc
```

### 에러 3: "CnC file not created"

```
Timeout waiting for MediaDriver to be ready
```

**원인**: ArchivingMediaDriver 미실행 또는 Java 문제

**해결**:
```bash
# ArchivingMediaDriver 확인
ps aux | grep ArchivingMediaDriver

# 없으면 시작
./scripts/start_archive_driver_local_distributed.sh

# Java 확인
java -version
```

---

## 🧹 정리 (Cleanup)

```bash
# 1. ArchivingMediaDriver 종료
pkill -f ArchivingMediaDriver

# 2. Subscriber/Publisher 종료 (Ctrl+C)

# 3. 디렉토리 정리
rm -rf /home/hesed/shm/aeron
rm -rf /dev/shm/aeron-sub*
rm -rf /home/hesed/shmaeron

# 4. 프로세스 확인
ps aux | grep aeron
```

---

## 🎯 Config 파일 선택 가이드

| Config 파일 | 용도 | 통신 방식 |
|-------------|------|-----------|
| `local-simple.ini` | **로컬 테스트 (권장)** | Localhost Unicast |
| `local-simple-publisher.ini` | **Publisher (권장)** | Localhost Unicast |
| `local-distributed-*.ini` | 로컬 분산 시뮬레이션 | Multicast (복잡) |
| `aeron-local.ini` | 기본 로컬 테스트 | Localhost Unicast |
| `aeron-distributed.ini` | 실제 분산 환경 | Multicast |

**권장**: 처음에는 `local-simple*.ini` 사용!

---

## 🔍 트러블슈팅 체크리스트

### 시작 전 확인

- [ ] ArchivingMediaDriver 실행 중? (`ps aux | grep ArchivingMediaDriver`)
- [ ] Java 설치? (`java -version`)
- [ ] 빌드 완료? (`ls build/publisher/aeron_publisher`)
- [ ] 디렉토리 존재? (`ls /home/hesed/shm/aeron`)

### Subscriber 시작 실패 시

- [ ] 다른 aeron-dir 사용? (`--aeron-dir /dev/shm/aeron-sub1`)
- [ ] Embedded 옵션? (`--embedded`)
- [ ] Config 파일 경로 정확? (`--config ../config/local-simple.ini`)

### 메시지 수신 안 될 때

- [ ] Publisher에서 `start` 입력?
- [ ] Archive 연결 성공? (Subscriber 출력 확인)
- [ ] 채널 설정 일치? (Publisher와 Subscriber config 비교)

---

## 📊 테스트 시나리오

### 시나리오 1: 기본 메시지 전송

1. 3개 터미널 실행
2. Publisher에서 `start`
3. Subscriber에서 메시지 수신 확인
4. 100개 메시지마다 레이턴시 통계 확인

### 시나리오 2: Recording & Replay

1. Publisher에서 `start` → 메시지 전송 → `stop`
2. Subscriber 종료
3. 새 Subscriber를 Replay 모드로 시작:
```bash
./subscriber/aeron_subscriber \
  --config ../config/local-simple.ini \
  --embedded \
  --aeron-dir /dev/shm/aeron-sub3 \
  --replay 0
```

### 시나리오 3: 여러 Subscriber

1. Subscriber #1 시작
2. Publisher `start`
3. Subscriber #2 추가 시작 (다른 aeron-dir)
4. 두 Subscriber 모두 메시지 수신 확인

---

## 💡 핵심 포인트

### ✅ 로컬 테스트 (권장)

1. **ArchivingMediaDriver 먼저 시작**
2. **IPC 채널 사용** (`aeron:ipc`)
3. **Publisher/Subscriber 모두 같은 aeron-dir 사용** (`/home/hesed/shm/aeron`)
4. **Embedded driver 불필요** (모두 ArchivingMediaDriver 공유)
5. **가장 빠른 성능** (공유 메모리 직접 통신)

### 🌐 분산 환경 (실제 서버 여러 대)

1. **Subscriber에서 --embedded 사용**
2. **각 서버마다 다른 aeron-dir** 필요
3. **UDP/Multicast 채널 사용**

### ❌ 하지 말아야 할 것

1. **로컬에서** embedded + 같은 aeron-dir (active driver error)
2. **로컬에서** multicast 채널 사용 (interface error)
3. ArchivingMediaDriver 없이 Publisher 실행

---

## 🚀 한 줄 요약 (로컬 테스트)

```bash
# 터미널 1: ArchivingMediaDriver
./scripts/start_archive_driver_local_distributed.sh

# 터미널 2: Publisher
cd build && ./publisher/aeron_publisher --config ../config/local-simple-publisher.ini

# 터미널 3: Subscriber
cd build && ./subscriber/aeron_subscriber --config ../config/local-simple.ini --aeron-dir /home/hesed/shm/aeron
```

**완료!** 🎉

**성능**: Min 10μs, Avg ~600μs (IPC 공유 메모리)
