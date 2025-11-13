# 수정 사항 요약 (2025-11-13)

## 문제 상황

Subscriber 실행 시 다음 두 가지 에러 발생:

1. **Active Driver Error**:
   ```
   ActiveDriverException: ERROR - active driver detected
   ```

2. **Multicast Interface Error**:
   ```
   Unable to find multicast interface matching criteria: /0.0.0.0:0/32
   ```

## 근본 원인 분석

### 1. Active Driver Error
- **원인**: Subscriber가 `--embedded --aeron-dir /home/hesed/shm/aeron` 사용
- **문제점**: ArchivingMediaDriver와 동일한 aeron-dir에 또 다른 embedded driver 시작 시도
- **결과**: 한 디렉토리에 두 개의 MediaDriver가 동시에 존재할 수 없음

### 2. Multicast Interface Error
- **원인**: Config 파일에 multicast 채널 (`224.0.1.1:40456`) 설정
- **문제점**: WSL2 로컬 환경에서 multicast 라우팅 설정되지 않음
- **결과**: Subscriber가 multicast 인터페이스를 찾을 수 없음

### 3. 하드코딩된 채널 설정
- **원인**: `AeronSubscriber::startLive()`가 `AeronConfig::SUBSCRIPTION_CHANNEL` 하드코딩 사용
- **문제점**: Config 파일에서 subscription 채널을 로드해도 실제로는 사용되지 않음
- **결과**: Config 파일 변경이 적용되지 않음

## 적용된 수정 사항

### 1. Subscriber Config 구조체 확장

**파일**: `subscriber/include/AeronSubscriber.h`

```cpp
struct SubscriberConfig {
    std::string aeron_dir = "/dev/shm/aeron";
    bool use_embedded_driver = false;
    std::string archive_control_channel = "";
    std::string subscription_channel = "";     // 추가
    int subscription_stream_id = 10;           // 추가
};
```

### 2. Subscriber가 Config 채널 사용하도록 수정

**파일**: `subscriber/src/AeronSubscriber.cpp`

```cpp
bool AeronSubscriber::startLive() {
    const std::string& channel = config_.subscription_channel.empty()
        ? AeronConfig::SUBSCRIPTION_CHANNEL
        : config_.subscription_channel;  // Config 파일 값 우선 사용

    return replay_to_live_handler_->startLive(
        channel,
        config_.subscription_stream_id
    );
}
```

동일한 수정을 `startReplay()`에도 적용

### 3. Main 함수에서 Config 로딩

**파일**: `subscriber/src/main.cpp`

```cpp
sub_config.subscription_channel = aeron_settings.subscription_channel;
sub_config.subscription_stream_id = aeron_settings.subscription_stream_id;
```

### 4. Config 파일을 IPC로 변경

**파일**: `config/local-simple.ini`, `config/local-simple-publisher.ini`

```ini
[publication]
# Multicast/UDP 대신 IPC (shared memory) 사용
channel = aeron:ipc
stream_id = 10

[subscription]
# IPC 채널 (가장 빠른 로컬 통신)
channel = aeron:ipc
stream_id = 10
```

**변경 이유**:
- IPC는 같은 서버 내 프로세스 간 공유 메모리로 통신
- 네트워크 스택 우회 → 가장 낮은 레이턴시
- 포트 바인딩 불필요 → "Address already in use" 에러 없음
- Multicast 설정 불필요 → 로컬 테스트에 최적

### 5. 문서 업데이트

**파일**: `QUICK_START.md`

- Subscriber 실행 방법 수정: `--embedded` 제거, 같은 aeron-dir 사용
- 에러 해결 가이드 업데이트
- 로컬 vs 분산 환경 구분 명확화

## 최종 작동 방식

### 로컬 테스트 구성 (Single Server)

```
┌─────────────────────────────────────┐
│  ArchivingMediaDriver (Java)        │
│  aeron-dir: /home/hesed/shm/aeron   │
│  - MediaDriver                       │
│  - Archive                           │
└─────────────────────────────────────┘
            ↓ IPC (shared memory)
┌─────────────────────────────────────┐
│  Publisher (C++)                     │
│  aeron-dir: /home/hesed/shm/aeron   │ ← 동일 dir
│  channel: aeron:ipc                  │
└─────────────────────────────────────┘
            ↓ IPC
┌─────────────────────────────────────┐
│  Subscriber (C++)                    │
│  aeron-dir: /home/hesed/shm/aeron   │ ← 동일 dir
│  channel: aeron:ipc                  │
│  NO embedded driver                  │
└─────────────────────────────────────┘
```

**핵심**:
- 모든 프로세스가 **같은 aeron-dir** 공유
- **IPC 채널** 사용 (공유 메모리)
- Subscriber는 **embedded driver 없음**
- 가장 빠른 성능 (Min: 10μs, Avg: ~600μs)

### 분산 환경 구성 (Multiple Servers)

```
Server 1 (Publisher):
┌─────────────────────────────────────┐
│  ArchivingMediaDriver               │
│  aeron-dir: /home/hesed/shm/aeron   │
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│  Publisher                           │
│  channel: aeron:udp?endpoint=...    │
└─────────────────────────────────────┘

Server 2 (Subscriber):
┌─────────────────────────────────────┐
│  Embedded MediaDriver (Java)         │
│  aeron-dir: /dev/shm/aeron-sub1     │ ← 독립 dir
└─────────────────────────────────────┘
            ↓
┌─────────────────────────────────────┐
│  Subscriber                          │
│  --embedded                          │
│  channel: aeron:udp?endpoint=...    │
└─────────────────────────────────────┘
```

## 테스트 결과

### 성공 확인

```
========================================
Subscriber Configuration
========================================
Aeron directory: /home/hesed/shm/aeron
Embedded driver: NO
Archive control: aeron:udp?endpoint=localhost:8010
Subscription channel: aeron:ipc
Mode: LIVE
========================================

Initializing Subscriber...
Connected to Aeron
Connected to Archive
Subscriber initialized successfully
Starting in LIVE mode...
Live subscription started
Subscriber running. Press Ctrl+C to exit.

========================================
Latency Statistics (298 samples)
========================================
Min:     10.53 μs
Max:     8651.30 μs
Average: 637.71 μs
========================================
```

### 성능 지표

- **최소 레이턴시**: 10.53 μs
- **평균 레이턴시**: 637.71 μs
- **최대 레이턴시**: 8.65 ms
- **메시지 수신**: 298개 (테스트 중)

## 수정된 파일 목록

1. `subscriber/include/AeronSubscriber.h` - SubscriberConfig 확장
2. `subscriber/src/AeronSubscriber.cpp` - Config 채널 사용
3. `subscriber/src/main.cpp` - Config 로딩
4. `config/local-simple.ini` - IPC 채널로 변경
5. `config/local-simple-publisher.ini` - IPC 채널로 변경
6. `QUICK_START.md` - 전면 수정
7. `FIXES_SUMMARY.md` - 이 파일 생성

## 교훈 및 권장 사항

### 로컬 테스트 시

1. **IPC 사용**: 가장 빠르고 설정 간단
2. **동일 aeron-dir**: Publisher/Subscriber 모두 ArchivingMediaDriver 공유
3. **Embedded 불필요**: 로컬에서는 ArchivingMediaDriver 하나면 충분

### 분산 환경 시

1. **Embedded MediaDriver**: 각 Subscriber가 독립 driver 필요
2. **다른 aeron-dir**: 각 서버/프로세스마다 독립적인 디렉토리
3. **UDP/Multicast**: 네트워크 통신 채널 사용

### Config 시스템

1. **우선순위**: CLI > Config File > AeronConfig.h
2. **검증**: 로드 후 설정 출력으로 확인
3. **분리**: 로컬/분산 환경별로 다른 config 파일 사용

## 다음 단계

- [ ] 실제 분산 환경에서 UDP/Multicast 테스트
- [ ] Replay 기능 테스트
- [ ] 여러 Subscriber 동시 실행 테스트
- [ ] Recording/Replay 성능 측정
- [ ] 프로덕션 환경 설정 가이드 작성
