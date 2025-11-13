# 변경 이력 (Changelog)

## 2025-11-13 (최신)

### 로컬 분산 환경 시뮬레이션 설정 업데이트

#### 스크립트 변경

**파일**: `scripts/start_archive_driver_local_distributed.sh`

##### 1. Aeron 디렉토리 변경
```bash
# 변경 전
AERON_DIR="/dev/shm/aeron-publisher"

# 변경 후
AERON_DIR="/home/hesed/shm/aeron"
```

**이유**:
- 기존 설정과 호환성 유지 (`/home/hesed/shm/aeron` 사용)
- 일반 파일시스템 사용 (tmpfs `/dev/shm` 대신)
- Publisher와 ArchivingMediaDriver가 같은 디렉토리 공유

##### 2. Archive 디렉토리 변경
```bash
# 변경 전
ARCHIVE_DIR="/dev/shm/aeron-archive"

# 변경 후
ARCHIVE_DIR="/home/hesed/shmaeron"
```

**이유**:
- 별도 디렉토리로 Archive 데이터 분리
- 기존 아카이브와 충돌 방지

**주의**: 디렉토리명에 오타 있음 (`shmaeron` → `shm/aeron-archive` 권장)

##### 3. 백그라운드 실행 추가
```bash
# 변경 전
io.aeron.archive.ArchivingMediaDriver 2>&1 | tee ${LOG_FILE}

# 변경 후
io.aeron.archive.ArchivingMediaDriver 2>&1 | tee ${LOG_FILE} &
```

**효과**:
- ✅ 터미널이 즉시 반환됨
- ✅ 같은 터미널에서 다른 명령 실행 가능
- ✅ 로그는 파일로 기록 (`logs/archive_driver_local_distributed.log`)
- ⚠️ Ctrl+C로 종료 불가 (pkill 사용 필요)

#### Config 파일 변경

**파일**: `config/local-distributed-publisher.ini`

```ini
# 변경 전
[aeron]
dir = /dev/shm/aeron-publisher

# 변경 후
[aeron]
dir = /home/hesed/shm/aeron
```

**영향**:
- Publisher가 ArchivingMediaDriver와 동일한 Aeron 디렉토리 사용
- 공유 메모리 통신 가능

#### 문서 업데이트

**파일**: `LOCAL_DISTRIBUTED_TEST.md`

변경 내용:
- ✅ 시뮬레이션 구조 다이어그램 업데이트
- ✅ 디렉토리 생성 명령 업데이트
- ✅ Config 파일 예제 업데이트
- ✅ 실행 방법에 백그라운드 실행 설명 추가
- ✅ 정리(Cleanup) 명령 업데이트
- ✅ 변경 이력 섹션 추가

---

## 2025-11-13 (초기)

### Config File System 구현

#### 새 기능

1. **ConfigLoader 클래스**
   - INI 파일 파싱 (의존성 없음)
   - 환경변수 override 지원
   - 설정 검증 기능
   - 파일: `common/include/ConfigLoader.h`, `common/src/ConfigLoader.cpp`

2. **Config 파일**
   - `config/aeron-local.ini` - 로컬 테스트용
   - `config/aeron-distributed.ini` - 분산 환경용
   - `config/local-distributed-publisher.ini` - 로컬 시뮬레이션 Publisher
   - `config/local-distributed-subscriber.ini` - 로컬 시뮬레이션 Subscriber

3. **CLI 통합**
   - Publisher: `--config` 옵션 추가
   - Subscriber: `--config` 옵션 추가
   - `--print-config` 옵션으로 설정 확인

4. **우선순위 시스템**
   - 환경변수 > CLI 옵션 > Config 파일 > 기본값(AeronConfig.h)

#### 문서

- `CONFIG_GUIDE.md` - Config 파일 사용 가이드
- `LOCAL_DISTRIBUTED_TEST.md` - 로컬 분산 환경 시뮬레이션 가이드

---

### Embedded MediaDriver 구현

#### 새 기능

1. **Subscriber Embedded Driver**
   - Subscriber가 자체 MediaDriver fork/start
   - Java 프로세스 불필요
   - `--embedded` 옵션으로 활성화
   - 파일: `subscriber/include/AeronSubscriber.h`, `subscriber/src/AeronSubscriber.cpp`

2. **자동 관리**
   - 시작 시 MediaDriver fork
   - CnC 파일 생성 대기
   - 종료 시 자동 cleanup (SIGTERM → SIGKILL)

#### 문서

- `DISTRIBUTED_SETUP.md` - 분산 환경 설정 가이드

---

### Multicast 설정

#### AeronConfig.h 업데이트

```cpp
// Publication/Subscription: Multicast
static constexpr const char* PUBLICATION_CHANNEL =
    "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
static constexpr const char* SUBSCRIPTION_CHANNEL =
    "aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0";
```

**효과**:
- 1:N 통신 (1개 Publisher → N개 Subscriber)
- 네트워크 트래픽 최소화
- 분산 환경 지원

---

## 사용법 요약

### 현재 권장 실행 방법

#### 로컬 분산 시뮬레이션

```bash
# 1. ArchivingMediaDriver (백그라운드)
./scripts/start_archive_driver_local_distributed.sh

# 2. Publisher
./build/publisher/aeron_publisher --config config/local-distributed-publisher.ini

# 3. Subscriber #1
./build/subscriber/aeron_subscriber \
  --config config/local-distributed-subscriber.ini \
  --embedded \
  --aeron-dir /dev/shm/aeron-sub1

# 4. Subscriber #2
./build/subscriber/aeron_subscriber \
  --config config/local-distributed-subscriber.ini \
  --embedded \
  --aeron-dir /dev/shm/aeron-sub2
```

#### 실제 분산 환경

**Publisher 서버**:
```bash
# ArchivingMediaDriver
./scripts/start_archive_driver.sh

# Publisher
./build/publisher/aeron_publisher --config config/aeron-distributed.ini
```

**Subscriber 서버**:
```bash
# Subscriber (embedded driver)
./build/subscriber/aeron_subscriber --config config/aeron-distributed.ini --embedded
```

---

## 알려진 이슈

### 1. Archive 디렉토리명 오타
- **현재**: `/home/hesed/shmaeron`
- **권장**: `/home/hesed/shm/aeron-archive`
- **영향**: 작동에는 문제 없음, 가독성/일관성 개선 필요
- **수정 방법**: `scripts/start_archive_driver_local_distributed.sh` 10번째 줄 수정

### 2. 백그라운드 실행 종료 방법
- **문제**: Ctrl+C로 종료 불가
- **해결**: `pkill -f ArchivingMediaDriver` 사용
- **문서**: `LOCAL_DISTRIBUTED_TEST.md` 정리 섹션 참조

---

## 다음 계획

### 단기 (우선순위 높음)
- [ ] Archive 디렉토리명 오타 수정
- [ ] ArchivingMediaDriver 종료 스크립트 작성
- [ ] Multicast route 자동 설정 스크립트

### 중기
- [ ] 성능 벤치마크 자동화
- [ ] 메시지 손실 감지 기능
- [ ] Health check 기능

### 장기
- [ ] Docker 지원
- [ ] Kubernetes 배포 설정
- [ ] 모니터링 대시보드

---

## 참고 문서

- `CLAUDE.md` - 프로젝트 전체 가이드
- `PROGRESS.md` - 진행 상황
- `CONFIG_GUIDE.md` - Config 파일 사용법
- `DISTRIBUTED_SETUP.md` - 분산 환경 설정
- `LOCAL_DISTRIBUTED_TEST.md` - 로컬 시뮬레이션
- `TEST_REPORT.md` - 테스트 보고서
