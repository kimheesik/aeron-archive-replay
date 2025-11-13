# Config File 사용 가이드

**작성일**: 2025-11-13
**기능**: INI 파일 기반 설정 관리

---

## 개요

Aeron 프로젝트의 모든 설정을 **INI 파일**에서 관리할 수 있습니다.

### 장점

✅ **재컴파일 불필요** - 설정 변경 후 재빌드 없이 적용
✅ **환경별 관리** - 로컬/개발/프로덕션 환경 분리
✅ **버전 관리** - Git으로 설정 파일 관리
✅ **CLI Override** - 커맨드라인에서 특정 값만 재정의
✅ **환경변수 지원** - 환경변수로 override 가능

---

## Config 파일 위치

```
/home/hesed/devel/aeron/
└── config/
    ├── aeron-local.ini          # 로컬 테스트용 (localhost)
    ├── aeron-distributed.ini    # 분산 환경용 (multicast)
    └── custom.ini               # 사용자 정의 설정
```

---

## Config 파일 형식

### 예제: `config/aeron-local.ini`

```ini
# Aeron Configuration - Local Test
# 단일 서버에서 Publisher와 Subscriber 모두 실행

[aeron]
# Aeron 공유 메모리 디렉토리
dir = /dev/shm/aeron

[archive]
# Archive Control (localhost)
control_request_channel = aeron:udp?endpoint=localhost:8010
control_response_channel = aeron:udp?endpoint=localhost:0

[publication]
# Publication channel
channel = aeron:udp?endpoint=localhost:40456
stream_id = 10

[subscription]
# Subscription channel
channel = aeron:udp?endpoint=localhost:40456
stream_id = 10

[replay]
# Replay channel
channel = aeron:udp?endpoint=localhost:40457
stream_id = 20

[timeouts]
idle_sleep_ms = 1
message_timeout_ns = 10000000000
```

### 주석

- `#` 또는 `;`로 시작하는 줄은 주석
- 빈 줄은 무시됨

---

## 사용 방법

### 1️⃣ Publisher

#### Config 파일 사용

```bash
cd /home/hesed/devel/aeron/build

# Local config
./publisher/aeron_publisher --config ../config/aeron-local.ini

# Distributed config
./publisher/aeron_publisher --config ../config/aeron-distributed.ini
```

#### 기본값 사용 (AeronConfig.h)

```bash
# Config 파일 없이 실행 (AeronConfig.h의 값 사용)
./publisher/aeron_publisher
```

#### CLI로 Override

```bash
# Config 파일 + CLI override
./publisher/aeron_publisher \
  --config ../config/aeron-local.ini \
  --pub-channel "aeron:udp?endpoint=224.0.1.2:40456"
```

#### Config 출력만

```bash
# 설정만 확인하고 종료
./publisher/aeron_publisher --config ../config/aeron-local.ini --print-config
```

### 2️⃣ Subscriber

#### Config 파일 + Embedded Driver

```bash
# Local config
./subscriber/aeron_subscriber --config ../config/aeron-local.ini --embedded

# Distributed config
./subscriber/aeron_subscriber --config ../config/aeron-distributed.ini --embedded
```

#### Replay 모드

```bash
./subscriber/aeron_subscriber \
  --config ../config/aeron-local.ini \
  --replay 0
```

#### CLI Override

```bash
./subscriber/aeron_subscriber \
  --config ../config/aeron-distributed.ini \
  --embedded \
  --archive-control "aeron:udp?endpoint=192.168.1.10:8010"
```

---

## CLI 옵션

### Publisher

```
--config <file>              INI 파일 경로
--aeron-dir <path>           Aeron 디렉토리 (override)
--pub-channel <channel>      Publication channel (override)
--pub-stream-id <id>         Publication stream ID (override)
--archive-control <channel>  Archive control channel (override)
--interval <ms>              메시지 전송 간격 (default: 100)
--print-config               설정 출력하고 종료
-h, --help                   도움말
```

### Subscriber

```
--config <file>              INI 파일 경로
--aeron-dir <path>           Aeron 디렉토리 (override)
--embedded                   Embedded MediaDriver 사용
--archive-control <channel>  Archive control channel (override)
--replay <position>          Replay 모드 시작 위치
--print-config               설정 출력하고 종료
-h, --help                   도움말
```

---

## 환경변수 Override

Config 파일보다 **환경변수가 우선**합니다.

### 지원 환경변수

```bash
export AERON_DIR=/custom/path/aeron
export AERON_ARCHIVE_CONTROL=aeron:udp?endpoint=192.168.1.10:8010
export AERON_PUBLICATION_CHANNEL=aeron:udp?endpoint=224.0.1.1:40456
export AERON_SUBSCRIPTION_CHANNEL=aeron:udp?endpoint=224.0.1.1:40456
```

### 우선순위

1. **환경변수** (최우선)
2. **CLI 옵션**
3. **Config 파일**
4. **AeronConfig.h 기본값** (최후)

---

## 환경별 Config 파일

### 로컬 개발 (`aeron-local.ini`)

- Publisher와 Subscriber가 **같은 서버**
- `localhost` 채널 사용
- 빠른 테스트용

```bash
# Publisher
./publisher/aeron_publisher --config ../config/aeron-local.ini

# Subscriber (다른 터미널)
./subscriber/aeron_subscriber --config ../config/aeron-local.ini --embedded
```

### 분산 환경 (`aeron-distributed.ini`)

- Publisher와 Subscriber가 **다른 서버**
- Multicast 채널 사용
- Archive는 Publisher 서버 (192.168.1.10)

```bash
# Publisher 서버 (192.168.1.10)
./publisher/aeron_publisher --config ../config/aeron-distributed.ini

# Subscriber 서버 (192.168.1.20, 1.21, ...)
./subscriber/aeron_subscriber --config ../config/aeron-distributed.ini --embedded
```

### 프로덕션 (`custom.ini`)

```bash
# 사용자 정의 설정 작성
cp config/aeron-distributed.ini config/production.ini
nano config/production.ini

# 실행
./publisher/aeron_publisher --config ../config/production.ini
./subscriber/aeron_subscriber --config ../config/production.ini --embedded
```

---

## Config 검증

### 설정 확인

```bash
# Publisher 설정 확인
./publisher/aeron_publisher --config ../config/aeron-local.ini --print-config

# Subscriber 설정 확인
./subscriber/aeron_subscriber --config ../config/aeron-distributed.ini --print-config
```

### 출력 예제

```
========================================
Aeron Configuration
========================================
[aeron]
  dir = /dev/shm/aeron

[archive]
  control_request_channel = aeron:udp?endpoint=192.168.1.10:8010
  control_response_channel = aeron:udp?endpoint=localhost:0

[publication]
  channel = aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0
  stream_id = 10

[subscription]
  channel = aeron:udp?endpoint=224.0.1.1:40456|interface=0.0.0.0
  stream_id = 10

[replay]
  channel = aeron:udp?endpoint=localhost:40457
  stream_id = 20

[timeouts]
  idle_sleep_ms = 1
  message_timeout_ns = 10000000000
========================================
```

---

## 트러블슈팅

### "Cannot open config file"

**원인**: 파일 경로가 잘못되었거나 파일이 없음

**해결**:
```bash
# 절대 경로 사용
./publisher/aeron_publisher --config /home/hesed/devel/aeron/config/aeron-local.ini

# 또는 상대 경로 확인
ls -la ../config/
```

### "Configuration validation failed"

**원인**: Config 파일 값이 잘못됨

**해결**:
- 채널은 `aeron:` 프로토콜로 시작해야 함
- Stream ID는 양수여야 함
- 필수 섹션/키가 누락되지 않았는지 확인

### "Invalid line"

**원인**: INI 파일 형식 오류

**해결**:
```ini
# ❌ 잘못된 형식
key value

# ✅ 올바른 형식
key = value
```

---

## 고급 사용법

### Config 파일 템플릿 생성

ConfigLoader는 템플릿 생성 기능 포함:

```cpp
// C++ 코드에서
ConfigLoader::generateTemplate("config/my-config.ini", "distributed");
```

템플릿 타입:
- `"local"` - 로컬 테스트용
- `"distributed"` - 분산 환경용
- `"production"` - 프로덕션용

### 동적 Config 변경

Runtime에 config를 변경하려면:

1. Config 파일 수정
2. 애플리케이션 재시작 (재컴파일 불필요!)

---

## 예제 시나리오

### 시나리오 1: 로컬 테스트

```bash
# Terminal 1: ArchivingMediaDriver
./scripts/start_archive_driver.sh

# Terminal 2: Publisher
./build/publisher/aeron_publisher --config config/aeron-local.ini

# Terminal 3: Subscriber
./build/subscriber/aeron_subscriber --config config/aeron-local.ini --embedded
```

### 시나리오 2: 분산 환경 (2 서버)

**Publisher 서버 (192.168.1.10)**:
```bash
# Terminal 1
./scripts/start_archive_driver.sh

# Terminal 2
./build/publisher/aeron_publisher --config config/aeron-distributed.ini
```

**Subscriber 서버 (192.168.1.20)**:
```bash
./build/subscriber/aeron_subscriber --config config/aeron-distributed.ini --embedded
```

### 시나리오 3: IP 주소 Override

Publisher IP가 다를 경우:
```bash
./build/subscriber/aeron_subscriber \
  --config config/aeron-distributed.ini \
  --embedded \
  --archive-control "aeron:udp?endpoint=10.0.0.5:8010"
```

---

## 요약

| 항목 | 내용 |
|------|------|
| **Config 형식** | INI 파일 (섹션 + key=value) |
| **위치** | `config/` 디렉토리 |
| **우선순위** | 환경변수 > CLI > Config 파일 > 기본값 |
| **검증** | `--print-config` 옵션 |
| **재컴파일** | ❌ 불필요 |

**핵심**: Config 파일로 **환경별 설정을 분리**하고, **재컴파일 없이** 배포 환경을 변경할 수 있습니다!
