# 작업 목록 (TODO List)

**프로젝트**: Aeron 기반 고성능 메시징 시스템
**최종 업데이트**: 2025-11-20

---

## 우선순위 분류

- 🔴 **P0**: 프로덕션 필수 (Critical)
- 🟡 **P1**: 중요 (Important)
- 🟢 **P2**: 개선 사항 (Nice to have)

---

## 현재 진행 중 (In Progress)

### 없음
- 모든 핵심 기능 완료됨

---

## 다음 작업 (Next Up)

### 🔴 P0: 안정성 강화 (Stability)

#### 1. CRC32 체크섬 검증 구현
**우선순위**: P0 (Critical)
**예상 시간**: 2시간
**영향 범위**: Subscriber, MessageBuffer

**작업 내용**:
- [ ] `MessageBuffer.h:204` - `calculateChecksum()` 구현
  ```cpp
  uint32_t calculateChecksum() const {
      // CRC32 계산 로직
      return crc32(payload_, header_.payload_length);
  }
  ```
- [ ] `MessageWorker.cpp:188` - `verifyChecksum()` 구현
  ```cpp
  if (!buffer->verifyChecksum()) {
      stats_.invalid_messages++;
      pool_.release(buffer);
      return;
  }
  ```
- [ ] Publisher에서 checksum 계산 후 전송
- [ ] 통합 테스트로 검증

**파일**:
- `common/include/MessageBuffer.h`
- `subscriber/src/MessageWorker.cpp`
- `publisher/src/AeronPublisher.cpp`

**성공 기준**:
- ✅ Checksum 불일치 시 메시지 거부
- ✅ 유효 메시지는 정상 처리
- ✅ 성능 영향 < 200ns

---

#### 2. 중복 제거 LRU Eviction 구현
**우선순위**: P0 (Critical)
**예상 시간**: 3시간
**영향 범위**: MessageWorker

**작업 내용**:
- [ ] `MessageWorker.cpp:212` - `seen_sequences_` 크기 제한
  ```cpp
  // Option 1: 시간 기반 eviction
  std::map<uint64_t, timestamp> seen_sequences_;

  // Option 2: LRU 기반 eviction
  std::list<uint64_t> lru_list_;
  std::unordered_map<uint64_t, list::iterator> lru_map_;

  // Option 3: 고정 윈도우
  static constexpr size_t MAX_SEEN = 100000;
  if (seen_sequences_.size() > MAX_SEEN) {
      // 오래된 항목 제거
  }
  ```
- [ ] 메모리 사용량 모니터링 추가
- [ ] 성능 테스트 (eviction 오버헤드)

**파일**:
- `subscriber/src/MessageWorker.cpp`

**성공 기준**:
- ✅ `seen_sequences_` 크기가 MAX_SEEN 이하 유지
- ✅ 중복 제거 기능 정상 작동
- ✅ 성능 영향 < 100ns

---

### 🟡 P1: 기능 완성 (Feature Completion)

#### 3. 메시지 타입별 핸들러 구현
**우선순위**: P1 (Important)
**예상 시간**: 4시간
**영향 범위**: MessageWorker

**작업 내용**:
- [ ] `MessageWorker.cpp:276` - `handleOrderNew()` 구현
  ```cpp
  void handleOrderNew(const MessageBuffer* buffer) {
      // Parse order data
      // Validate order fields
      // Process order logic
      // Update stats
  }
  ```
- [ ] `MessageWorker.cpp:282` - `handleOrderExecution()` 구현
- [ ] `MessageWorker.cpp:288` - `handleOrderModify()` 구현
- [ ] `MessageWorker.cpp:294` - `handleOrderCancel()` 구현
- [ ] 각 핸들러에 대한 단위 테스트 작성

**파일**:
- `subscriber/src/MessageWorker.cpp`
- `common/include/MessageBuffer.h` (메시지 구조체 추가)

**참고 문서**:
- `MESSAGE_STRUCTURE_DESIGN.md` (메시지 포맷 정의)

**성공 기준**:
- ✅ 각 메시지 타입별 정상 처리
- ✅ 비즈니스 로직 검증
- ✅ 에러 핸들링 구현

---

#### 4. Sequence Gap 복구 로직
**우선순위**: P1 (Important)
**예상 시간**: 3시간
**영향 범위**: MessageWorker, Archive

**작업 내용**:
- [ ] Gap 감지 시 Archive에 재요청
  ```cpp
  void onGapDetected(uint64_t missing_start, uint64_t missing_end) {
      // 1. Archive에서 누락 구간 조회
      // 2. Replay 요청
      // 3. 누락 메시지 수신
      // 4. 정상 흐름 복귀
  }
  ```
- [ ] Gap 복구 중 Live 메시지 버퍼링
- [ ] 복구 완료 후 순서 보장

**파일**:
- `subscriber/src/MessageWorker.cpp`
- `subscriber/include/AeronSubscriber.h`

**성공 기준**:
- ✅ Gap 자동 복구
- ✅ 메시지 순서 보장
- ✅ Live 메시지 손실 없음

---

### 🟢 P2: 성능 최적화 (Performance)

#### 5. 처리량 측정 및 벤치마크
**우선순위**: P2 (Nice to have)
**예상 시간**: 2시간

**작업 내용**:
- [ ] 초당 메시지 처리량 측정
  ```cpp
  struct ThroughputStats {
      uint64_t messages_per_second;
      uint64_t bytes_per_second;
      double cpu_usage;
  };
  ```
- [ ] 다양한 메시지 크기 테스트 (64B, 256B, 1KB, 4KB)
- [ ] 장시간 안정성 테스트 (1시간, 1M+ 메시지)

**도구**:
- `test_message_publisher` 수정 (burst mode 추가)
- 새로운 벤치마크 스크립트 작성

**성공 기준**:
- ✅ 100K msg/sec 이상 처리
- ✅ 1시간 안정성 테스트 통과
- ✅ 메모리 누수 없음

---

#### 6. 레이턴시 분포 분석
**우선순위**: P2 (Nice to have)
**예상 시간**: 2시간

**작업 내용**:
- [ ] Percentile 통계 추가 (P50, P95, P99, P99.9)
  ```cpp
  struct LatencyDistribution {
      double p50_us;
      double p95_us;
      double p99_us;
      double p999_us;
  };
  ```
- [ ] 히스토그램 구현 (HdrHistogram 라이브러리 사용 고려)
- [ ] CSV 파일로 raw data 저장

**파일**:
- `subscriber/include/MessageStats.h`
- `subscriber/src/MessageWorker.cpp`

**성공 기준**:
- ✅ P99 < 5ms
- ✅ P999 < 10ms
- ✅ 레이턴시 분포 시각화 가능

---

### 🟢 P2: 문서화 (Documentation)

#### 7. API 레퍼런스 작성
**우선순위**: P2
**예상 시간**: 3시간

**작업 내용**:
- [ ] Publisher API 문서
  - `AeronPublisher` 클래스
  - `RecordingController` 클래스
  - 사용 예제
- [ ] Subscriber API 문서
  - `AeronSubscriber` 클래스
  - `MessageCallback` 인터페이스
  - Zero-copy 사용법
- [ ] MessageBuffer 프로토콜 문서
  - 헤더 필드 상세 설명
  - 메시지 타입별 구조

**산출물**:
- `docs/API_REFERENCE.md`

---

#### 8. 배포 가이드 작성
**우선순위**: P2
**예상 시간**: 2시간

**작업 내용**:
- [ ] 프로덕션 환경 설정 가이드
- [ ] 성능 튜닝 가이드
- [ ] 모니터링 설정
- [ ] 트러블슈팅 체크리스트

**산출물**:
- `docs/DEPLOYMENT_GUIDE.md`

---

#### 9. 운영 매뉴얼 작성
**우선순위**: P2
**예상 시간**: 3시간

**작업 내용**:
- [ ] 시스템 시작/종료 절차
- [ ] 백업/복구 절차
- [ ] 장애 대응 시나리오
- [ ] 로그 분석 방법

**산출물**:
- `docs/OPERATIONS_MANUAL.md`

---

## 향후 작업 (Future Work)

### 확장 기능

#### 10. 멀티캐스트 네트워크 지원
**우선순위**: P2
**예상 시간**: 1일

**작업 내용**:
- [ ] 멀티캐스트 채널 설정
- [ ] 라우팅 테이블 구성
- [ ] 네트워크 성능 테스트

---

#### 11. 다중 Publisher 지원
**우선순위**: P2
**예상 시간**: 1일

**작업 내용**:
- [ ] Publisher ID별 sequence 관리
- [ ] 중복 제거 로직 수정
- [ ] Load balancing 고려

---

#### 12. Grafana/Prometheus 통합
**우선순위**: P2
**예상 시간**: 2일

**작업 내용**:
- [ ] Prometheus exporter 구현
- [ ] Grafana 대시보드 작성
- [ ] Alert 규칙 정의

---

#### 13. 메시지 압축
**우선순위**: P2
**예상 시간**: 2일

**작업 내용**:
- [ ] LZ4 압축 통합
- [ ] 압축률 vs 레이턴시 트레이드오프 분석
- [ ] 선택적 압축 (크기 임계값)

---

## 코드 품질 개선 (Code Quality)

#### 14. 단위 테스트 작성
**우선순위**: P1
**예상 시간**: 1일

**작업 내용**:
- [ ] Google Test 통합
- [ ] MessageBuffer 테스트
- [ ] BufferPool 테스트
- [ ] MessageQueue 테스트
- [ ] MessageWorker 테스트

**목표 커버리지**: 80%

---

#### 15. 정적 분석 및 Linting
**우선순위**: P2
**예상 시간**: 4시간

**작업 내용**:
- [ ] clang-tidy 실행
- [ ] cppcheck 실행
- [ ] 메모리 누수 검사 (valgrind)
- [ ] Thread sanitizer 실행

---

## 완료된 작업 (Completed)

### Phase 1: 기본 메시징 ✅
- ✅ Aeron 기반 Publisher/Subscriber 구현
- ✅ INI 파일 설정 시스템
- ✅ UDP 유니캐스트 메시지 전송
- ✅ 내부 레이턴시 측정

### Phase 2: Recording/Replay ✅
- ✅ ArchivingMediaDriver 통합
- ✅ Recording 시작/중지 제어
- ✅ ReplayMerge API 마이그레이션
- ✅ Auto-discovery 기능
- ✅ Replay → Live 자동 전환
- ✅ Gap detection

### Phase 3: 모니터링 ✅
- ✅ SPSC Queue 구현
- ✅ MessageCallback API
- ✅ 모니터링 예제 작성
- ✅ 성능 테스트 (0.009% 오버헤드)

### Phase 4: Zero-Copy ✅
- ✅ MessageBuffer 프로토콜 구현
- ✅ Publisher 수정 (MessageBuffer 형식)
- ✅ Buffer Pool 구현
- ✅ Message Queue 구현
- ✅ 3-스레드 아키텍처
- ✅ 통합 테스트 (20/20 메시지 성공)

### Phase 5: 문서 정리 ✅
- ✅ `.claude/` 디렉토리 생성
- ✅ `PROJECT_STATUS.md` 작성
- ✅ `ARCHITECTURE.md` 작성
- ✅ `TODO.md` 작성 (이 문서)
- ⏳ `DECISIONS.md` 작성 중
- ⏳ 중복 문서 정리 예정

---

## 빠른 참조 (Quick Reference)

### TODO 항목별 파일 위치

| TODO | 파일 | 라인 |
|------|------|------|
| CRC32 검증 | `common/include/MessageBuffer.h` | 204 |
| CRC32 검증 | `subscriber/src/MessageWorker.cpp` | 188 |
| LRU eviction | `subscriber/src/MessageWorker.cpp` | 212 |
| handleOrderNew | `subscriber/src/MessageWorker.cpp` | 276 |
| handleOrderExecution | `subscriber/src/MessageWorker.cpp` | 282 |
| handleOrderModify | `subscriber/src/MessageWorker.cpp` | 288 |
| handleOrderCancel | `subscriber/src/MessageWorker.cpp` | 294 |

### 예상 일정

**주요 마일스톤**:
- 2025-11-21: CRC32 검증 완료
- 2025-11-22: LRU eviction 완료
- 2025-11-23: 메시지 핸들러 완료
- 2025-11-24: Gap 복구 완료
- 2025-11-25: 프로덕션 준비 완료 🎉

**총 예상 시간**: 약 30시간 (P0~P1 항목만)

---

**마지막 업데이트**: 2025-11-20
**다음 리뷰**: 2025-11-22
