# Aeron 메시징 시스템 - 빠른 시작 가이드

**프로젝트 위치**: `/home/hesed/devel/aeron`
**최종 업데이트**: 2025-11-11

---

## 📋 체크리스트

시작하기 전에 확인:
- [ ] ArchivingMediaDriver 종료됨
- [ ] Publisher 종료됨
- [ ] Subscriber 종료됨
- [ ] 3개의 터미널 준비됨

---

## 🚀 시작하기 (3단계)

### Terminal 1: ArchivingMediaDriver

```bash
cd /home/hesed/devel/aeron
./scripts/start_archive_driver.sh
```

**성공 표시**:
```
Starting Aeron ArchivingMediaDriver
Aeron Directory: /home/hesed/shm/aeron
Archive Directory: /home/hesed/shm/aeron-archive
```

**이 터미널은 계속 실행 상태로 유지**

---

### Terminal 2: Publisher

```bash
cd /home/hesed/devel/aeron/build
./publisher/aeron_publisher --aeron-dir /home/hesed/shm/aeron
```

**성공 표시**:
```
Connected to Aeron
Publication ready: aeron:udp?endpoint=localhost:40456
Connected to Archive
Publisher initialized successfully
```

**명령어**:
- `start` - Recording 시작
- `stop` - Recording 중지
- `quit` - 종료

**Recording 시작 방법**:
```
> start
Starting recording on channel: aeron:udp?endpoint=localhost:40456, streamId: 10
```

---

### Terminal 3: Subscriber

#### 옵션 A: Live 모드 (실시간)
```bash
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber
```

#### 옵션 B: Replay 모드 (녹화 재생)
```bash
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber --replay 0
```

**성공 표시 (Replay)**:
```
Starting in REPLAY mode from position: 0
Found recording ID: 0, stopPosition: 381024
Replay started. Session ID: 9909375076
Live subscription pre-created
```

**레이턴시 통계** (1000개 메시지마다 출력):
```
========================================
Latency Statistics (1000 samples)
========================================
Min:     323.07 μs
Max:     2575.30 μs
Average: 1232.90 μs
========================================
```

---

## 🧹 정리하기

모든 터미널에서 `Ctrl+C` 또는:

```bash
pkill -f "aeron_publisher|aeron_subscriber|ArchivingMediaDriver"
```

---

## ⚠️ 주의사항

### 1. 실행 순서가 중요합니다!
1. ArchivingMediaDriver (먼저)
2. Publisher
3. Subscriber

### 2. Publisher 옵션 필수
```bash
--aeron-dir /home/hesed/shm/aeron
```
이 옵션을 빼먹으면 "CnC file not created" 에러 발생!

### 3. Recording 전에 메시지 발행
Publisher가 메시지를 보내야 Recording이 생성됩니다.

---

## 🔍 상태 확인

### 프로세스 확인
```bash
ps aux | grep -E "aeron|ArchivingMediaDriver"
```

### Recording 파일 확인
```bash
ls -lh /home/hesed/shm/aeron-archive/
```

### Aeron 디렉토리 확인
```bash
ls -la /home/hesed/shm/aeron/
```

---

## 📖 더 보기

- **PROGRESS.md** - 전체 진행상황 및 작업 목록
- **TEST_REPORT.md** - 상세 테스트 결과
- **CLAUDE.md** - 프로젝트 전체 가이드
- **README.md** - 개발 문서

---

## 🐛 문제 해결

### "CnC file not created"
→ ArchivingMediaDriver 먼저 시작하고 `--aeron-dir` 옵션 확인

### "Failed to connect to Archive"
→ ArchivingMediaDriver 실행 중인지 확인 (Terminal 1)

### "No recording found"
→ Publisher에서 `start` 명령 실행 후 몇 초 대기

---

**완료!** 시스템이 정상적으로 작동하면 다음을 확인할 수 있습니다:
- ✅ Publisher: 메시지 발행 중
- ✅ Subscriber: 메시지 수신 중 (레이턴시 통계 출력)
- ✅ Archive: Recording 파일 생성 중
