# 주요 설계 결정사항 (Design Decisions)

**프로젝트**: Aeron 기반 고성능 메시징 시스템
**작성일**: 2025-11-20

---

## 목차

1. [아키텍처 결정](#아키텍처-결정)
2. [성능 관련 결정](#성능-관련-결정)
3. [프로토콜 설계 결정](#프로토콜-설계-결정)
4. [구현 기술 선택](#구현-기술-선택)
5. [변경 이력](#변경-이력)

---

## 아키텍처 결정

### ADR-001: 3-Process 아키텍처 선택

**일자**: 2025-11-09
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- Publisher, Subscriber, MediaDriver를 어떻게 구성할 것인가?
- 단일 프로세스 vs 다중 프로세스

**결정**:
3개의 독립 프로세스로 분리
1. **ArchivingMediaDriver** (Java) - 별도 프로세스
2. **Publisher** (C++) - 독립 프로세스
3. **Subscriber** (C++) - 독립 프로세스

**근거**:
- **장점**:
  - 프로세스 격리로 안정성 향상
  - 독립적 확장 가능 (각각 스케일 아웃)
  - ArchivingMediaDriver는 Java로만 제공됨 (공식 지원)
  - 각 프로세스 독립 재시작 가능
- **단점**:
  - 관리 복잡도 증가
  - 프로세스 간 통신 오버헤드

**대안**:
- ~~Embedded MediaDriver in Publisher~~ - Recording 제어 복잡도 증가
- ~~All-in-one 프로세스~~ - 장애 격리 불가

**영향**:
- 운영 복잡도: 시작 순서 관리 필요 (Driver → Publisher → Subscriber)
- 모니터링: 3개 프로세스 각각 모니터링 필요

---

### ADR-002: Subscriber에 Embedded MediaDriver 사용

**일자**: 2025-11-14
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- Subscriber가 ArchivingMediaDriver를 사용할 것인가, 자체 MediaDriver를 사용할 것인가?
- 초기에는 공유 MediaDriver 사용 → 문제 발생

**결정**:
Subscriber는 **항상 Embedded MediaDriver** 사용 (필수)

**근거**:
- **문제점** (공유 MediaDriver 사용 시):
  - ReplayMerge가 정상 작동하지 않음
  - Replay → Live 전환 실패
  - Image 관리 문제
- **해결책** (Embedded MediaDriver):
  - Subscriber가 자체 MediaDriver 소유
  - ReplayMerge API 정상 작동
  - Image lifecycle 제어 가능

**영향**:
- Subscriber 프로세스당 하나의 MediaDriver 인스턴스
- 메모리 사용량 약간 증가 (허용 범위)
- 안정성 크게 향상

**참고 문서**: `EMBEDDED_MEDIADRIVER_MIGRATION.md`

---

### ADR-003: Zero-Copy 3-Thread 아키텍처

**일자**: 2025-11-19
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- 메시지 처리 성능을 극대화하기 위한 아키텍처 선택
- Single-thread vs Multi-thread

**결정**:
**3-Thread 아키텍처** 채택:
1. **Main Thread**: Aeron 수신 + Queue enqueue
2. **Worker Thread**: Queue dequeue + 메시지 처리
3. **Monitoring Thread**: 통계 수집 + 출력

**근거**:
- **장점**:
  - Main Thread가 Aeron 수신에만 집중 → 메시지 손실 최소화
  - Worker Thread가 비즈니스 로직 처리 → CPU 활용 최적화
  - Monitoring Thread 분리 → 통계 출력이 수신에 영향 없음
  - Lock-free Queue로 스레드 간 통신 → 오버헤드 최소
- **측정 결과**:
  - 처리 시간: 0.76 μs (마이크로초)
  - 총 오버헤드: 0.009%
  - Queue 사용률: 0%

**대안**:
- ~~Single Thread~~ - 비즈니스 로직이 수신을 블록할 수 있음
- ~~4+ Threads~~ - 오버엔지니어링, 복잡도 증가

**영향**:
- 스레드 동기화 필요 (Lock-free Queue로 해결)
- CPU 코어 3개 사용

---

## 성능 관련 결정

### ADR-004: Lock-Free SPSC Queue 사용

**일자**: 2025-11-18
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- 스레드 간 메시지 전달 방법 선택
- Mutex vs Lock-free vs Lockless

**결정**:
**Single Producer Single Consumer (SPSC) Lock-free Queue** 사용

**근거**:
- **성능**:
  - Enqueue: ~50 ns
  - Dequeue: ~50 ns
  - Mutex 기반 Queue 대비 10배 빠름
- **단순성**:
  - Producer 1개, Consumer 1개로 제약 명확
  - 복잡한 동기화 불필요
- **Cache-friendly**:
  - 64-byte alignment로 false sharing 방지
  - Memory ordering (acquire/release) 최적화

**구현**:
```cpp
template<typename T, uint32_t Capacity>
class SPSCQueue {
    alignas(64) std::atomic<uint32_t> write_index_;
    alignas(64) std::atomic<uint32_t> read_index_;
    std::array<T, Capacity> buffer_;
};
```

**대안**:
- ~~std::queue + std::mutex~~ - 성능 저하 (100배 느림)
- ~~Boost lockfree queue~~ - 의존성 증가, SPSC는 직접 구현 가능

**영향**:
- Producer/Consumer 1:1 제약 (현재 요구사항에 부합)
- 추후 N:M 필요 시 다른 Queue 사용 고려

---

### ADR-005: Buffer Pool 메모리 관리

**일자**: 2025-11-19
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- 메시지 버퍼를 매번 할당/해제할 것인가, 재사용할 것인가?

**결정**:
**Buffer Pool 패턴** 사용 (Object Pool)
- 사전 할당: 1024 buffers x 4096 bytes = 4 MB
- Free list 관리
- acquire() / release() API

**근거**:
- **성능**:
  - 동적 할당 제거 → 레이턴시 안정화
  - acquire/release: O(1) 시간 복잡도
- **메모리 효율**:
  - 고정 메모리 사용 (4 MB)
  - 메모리 파편화 방지
- **실측 결과**:
  - 할당/해제 실패: 0건
  - 효율: 100%

**대안**:
- ~~매번 new/delete~~ - 성능 저하, 파편화
- ~~std::allocator~~ - 충분하지 않음

**영향**:
- 초기 메모리 4 MB 사용 (허용 범위)
- Pool 크기 조정 가능 (1024는 충분함)

---

### ADR-006: Aeron 공유 메모리 위치 변경

**일자**: 2025-11-13
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- Aeron 공유 메모리를 어디에 둘 것인가?
- `/dev/shm` (tmpfs) vs `/home/hesed/shm` (일반 디스크)

**결정**:
`/home/hesed/shm/aeron` 사용 (일반 디스크)

**근거**:
- **문제점** (`/dev/shm` 사용 시):
  - WSL2 환경에서 `/dev/shm` 크기 제한 (기본 64MB)
  - 용량 초과 시 프로그램 실패
- **해결책** (`/home/hesed/shm` 사용):
  - 크기 제한 없음
  - WSL2 환경에서도 안정적
- **성능 영향**:
  - 로컬 테스트 환경에서는 무시 가능
  - 프로덕션에서는 `/dev/shm` 사용 권장 (설정으로 변경 가능)

**설정**:
```ini
aeron.dir=/home/hesed/shm/aeron
aeron.archive.dir=/home/hesed/shm/aeron-archive
```

**대안**:
- ~~`/dev/shm` 크기 확장~~ - WSL2에서 제약
- ~~RAM disk 생성~~ - 복잡도 증가

**영향**:
- 프로덕션 배포 시 `/dev/shm` 사용으로 변경 필요 (설정 파일로 가능)

---

## 프로토콜 설계 결정

### ADR-007: 64-Byte 고정 헤더 + 가변 본문 구조

**일자**: 2025-11-18
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- 메시지 프로토콜 구조 설계
- 고정 길이 vs 가변 길이 vs 혼합

**결정**:
**64-byte 고정 헤더 + 가변 길이 본문** 구조

**근거**:
- **64-byte 헤더 선택 이유**:
  - Cache line 크기 (64 bytes)에 정확히 맞춤
  - CPU 캐시 효율 극대화
  - 단일 메모리 접근으로 헤더 전체 읽기 가능
- **고정 헤더 장점**:
  - 파싱 속도 빠름 (오프셋 계산 불필요)
  - Zero-copy 가능
  - 타입 안전성
- **가변 본문 장점**:
  - 유연한 페이로드 크기
  - 메시지 타입별 최적 크기

**구조**:
```cpp
#pragma pack(push, 1)
struct MessageHeader {
    uint8_t  magic[4];          // "SEKR"
    uint16_t version;           // 0x0001
    uint16_t message_type;      // 99 = test
    uint64_t sequence_number;
    uint64_t event_time_ns;
    uint64_t publish_time_ns;
    uint64_t recv_time_ns;
    uint32_t message_length;
    uint16_t publisher_id;
    uint8_t  priority;
    uint8_t  flags;
    uint64_t session_id;
    uint32_t checksum;
    uint32_t reserved;
};  // 64 bytes
#pragma pack(pop)
```

**대안**:
- ~~전체 가변 길이~~ - 파싱 복잡, 성능 저하
- ~~전체 고정 길이~~ - 메모리 낭비

**영향**:
- 헤더 필드 추가 시 reserved 영역 사용
- 버전 관리 필요 (version 필드로 구분)

---

### ADR-008: Sequence Number 기반 중복 제거

**일자**: 2025-11-19
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- 메시지 중복 제거 방법 선택
- Sequence number vs UUID vs Timestamp

**결정**:
**Monotonic Sequence Number** 기반 중복 제거

**근거**:
- **장점**:
  - 빠른 비교 (uint64_t 비교)
  - Gap 감지 가능 (순차 증가)
  - 메모리 효율적 (8 bytes)
  - 순서 보장 가능
- **구현**:
  ```cpp
  std::atomic<uint64_t> next_sequence_{1};
  sequence_number = next_sequence_.fetch_add(1);
  ```
- **중복 체크**:
  ```cpp
  std::unordered_set<uint64_t> seen_sequences_;
  if (seen_sequences_.count(seq) > 0) {
      return; // 중복
  }
  ```

**대안**:
- ~~UUID~~ - 크기 크고 (16 bytes), Gap 감지 불가
- ~~Timestamp~~ - 중복 가능성 있음 (동일 시각)

**영향**:
- Publisher 재시작 시 sequence 초기화 → Session ID로 구분
- 다중 Publisher 시 Publisher ID별로 sequence 관리 필요

---

### ADR-009: MessageBuffer를 common으로 이동

**일자**: 2025-11-19
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- MessageBuffer.h를 어디에 둘 것인가?
- Subscriber only vs Common

**결정**:
`common/include/MessageBuffer.h`로 이동 (공유)

**근거**:
- **문제점** (Subscriber only):
  - Publisher가 MessageBuffer 구조를 알 수 없음
  - 코드 중복 발생
  - 프로토콜 불일치 가능성
- **해결책** (Common):
  - Publisher와 Subscriber가 동일한 정의 사용
  - 단일 진실 공급원 (Single Source of Truth)
  - 프로토콜 버전 관리 용이

**영향**:
- Publisher CMakeLists.txt에 common include 경로 추가
- 프로토콜 변경 시 양쪽 모두 재빌드 필요 (의도된 동작)

---

## 구현 기술 선택

### ADR-010: ReplayMerge 수동 구현 → 공식 API 마이그레이션

**일자**: 2025-11-14
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- ReplayMerge를 어떻게 구현할 것인가?
- 초기: 수동 구현 (2개 subscription)
- 변경: 공식 API 사용

**결정**:
Aeron Archive의 **공식 ReplayMerge API** 사용

**근거**:
- **수동 구현 문제점**:
  - 복잡한 상태 관리 (REPLAY → TRANSITIONING → LIVE)
  - 코드 라인 수 많음 (~600 lines)
  - 에지 케이스 처리 어려움
  - 버그 가능성 높음
- **공식 API 장점**:
  - 검증된 구현 (Aeron 팀 제공)
  - 코드 67% 감소 (~200 lines)
  - Auto-discovery 기능 내장
  - 안정성 향상

**마이그레이션 결과**:
- Before: `ReplayToLiveHandler` (복잡)
- After: `archive->startReplay()` (단순)

**참고 문서**: `REPLAYMERGE_MIGRATION.md`

---

### ADR-011: C++17 표준 사용

**일자**: 2025-11-09
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- C++ 표준 버전 선택
- C++11 vs C++14 vs C++17 vs C++20

**결정**:
**C++17** 사용

**근거**:
- **필요 기능**:
  - `std::optional` - 선택적 반환값
  - `std::string_view` - 문자열 복사 방지
  - Structured bindings - 코드 가독성
  - `if constexpr` - 컴파일 타임 분기
- **호환성**:
  - GCC 8+ 지원 (개발 환경)
  - Aeron C++ API와 호환
- **안정성**:
  - C++17은 충분히 성숙함
  - C++20은 아직 일부 환경에서 미지원

**대안**:
- ~~C++11~~ - 최신 기능 부족
- ~~C++20~~ - 호환성 문제 가능성

**영향**:
- CMakeLists.txt: `set(CMAKE_CXX_STANDARD 17)`
- 컴파일러 버전: GCC 8+ 또는 Clang 10+ 필요

---

### ADR-012: INI 파일 기반 설정 시스템

**일자**: 2025-11-10
**상태**: ✅ 승인됨 (Accepted)

**컨텍스트**:
- 설정 관리 방법 선택
- 하드코딩 vs 설정 파일 vs 환경 변수

**결정**:
**INI 파일 기반 설정** + CLI 인자 오버라이드

**근거**:
- **INI 파일 장점**:
  - 사람이 읽기 쉬움
  - 섹션별 구조화 가능
  - 주석 지원
  - 버전 관리 가능 (Git)
- **구현**:
  - `ConfigLoader.cpp` (405 lines)
  - 우선순위: CLI args > INI file > Default
- **사용 예**:
  ```bash
  ./aeron_publisher --config aeron-local.ini --interval 10
  ```

**대안**:
- ~~JSON/YAML~~ - 파싱 라이브러리 의존성
- ~~환경 변수~~ - 관리 어려움
- ~~하드코딩~~ - 유연성 없음

**영향**:
- 설정 파일 관리 필요
- 다양한 환경 설정 가능 (local, distributed, production)

---

## 변경 이력

### 주요 변경사항

| 일자 | ADR | 변경 내용 | 이유 |
|------|-----|-----------|------|
| 2025-11-14 | ADR-002 | Subscriber Embedded MediaDriver 필수화 | ReplayMerge 안정성 |
| 2025-11-14 | ADR-010 | ReplayMerge 공식 API 마이그레이션 | 코드 단순화, 안정성 |
| 2025-11-19 | ADR-003 | 3-Thread 아키텍처 도입 | Zero-copy 성능 |
| 2025-11-19 | ADR-009 | MessageBuffer common으로 이동 | 프로토콜 통일 |

### 폐기된 결정

#### ~~ADR-XXX: Subscriber가 ArchivingMediaDriver 공유~~
**폐기일**: 2025-11-14
**이유**: ReplayMerge 작동 불가
**대체**: ADR-002 (Embedded MediaDriver)

#### ~~ADR-XXX: 수동 ReplayMerge 구현~~
**폐기일**: 2025-11-14
**이유**: 복잡도 높음, 버그 가능성
**대체**: ADR-010 (공식 API 사용)

---

## 의사결정 프로세스

### 새로운 ADR 작성 시

1. **문제 정의**: 해결하려는 문제가 무엇인가?
2. **대안 검토**: 최소 2개 이상 대안 검토
3. **실험/프로토타입**: 가능하면 간단한 테스트
4. **근거 문서화**: 왜 이 결정을 내렸는가?
5. **영향 분석**: 시스템에 어떤 영향을 미치는가?
6. **리뷰**: 팀/동료 리뷰 (1인 프로젝트는 생략 가능)
7. **승인 및 문서화**: 이 문서에 추가

### ADR 템플릿

```markdown
### ADR-XXX: [제목]

**일자**: YYYY-MM-DD
**상태**: 🟡 제안됨 / ✅ 승인됨 / ❌ 거부됨 / 🔄 대체됨

**컨텍스트**:
- 문제 설명

**결정**:
- 선택한 방안

**근거**:
- 장점
- 단점
- 측정 결과 (있으면)

**대안**:
- 대안 1
- 대안 2

**영향**:
- 시스템 영향
- 운영 영향
```

---

## 참고 자료

### 관련 문서
- `ARCHITECTURE.md` - 시스템 아키텍처
- `TODO.md` - 미결정 항목
- `REPLAYMERGE_MIGRATION.md` - ADR-010 상세
- `EMBEDDED_MEDIADRIVER_MIGRATION.md` - ADR-002 상세

### 외부 참조
- [Aeron Architecture](https://github.com/real-logic/aeron/wiki)
- [Lock-free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
- [Architecture Decision Records](https://adr.github.io/)

---

**문서 버전**: 1.0
**최종 업데이트**: 2025-11-20
**다음 리뷰**: 새로운 결정 사항 발생 시
