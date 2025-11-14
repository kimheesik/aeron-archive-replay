# Embedded MediaDriver Migration Plan

## Overview

Migration from external Java ArchivingMediaDriver to embedded C++ MediaDriver architecture with distributed Publisher/Subscriber setup.

## Target Architecture

```
┌─────────────────────────────────────┐     ┌──────────────────────────────┐
│   Publisher Server                  │     │   Subscriber Server          │
│                                     │     │                              │
│  ┌──────────────────────────────┐  │     │  ┌────────────────────────┐  │
│  │  Java ArchivingMediaDriver   │  │     │  │  C++ Embedded          │  │
│  │  - Archive recordings        │  │     │  │  MediaDriver           │  │
│  │  - Replay service            │  │     │  │  (aeronmd)             │  │
│  │  - localhost:8010            │  │     │  └────────────────────────┘  │
│  └──────────────────────────────┘  │     │            ▲                 │
│              ▲                      │     │            │                 │
│              │                      │     │  ┌────────────────────────┐  │
│  ┌──────────────────────────────┐  │     │  │  aeron_subscriber      │  │
│  │  aeron_publisher             │  │     │  │  - Embedded driver     │  │
│  │  - C++ Embedded MediaDriver  │  │     │  │  - ReplayMerge         │  │
│  │  - Archive control client    │  │     │  │  - Archive client      │  │
│  │  - Publication               │  │     │  │  - Subscription        │  │
│  └──────────────────────────────┘  │     │  └────────────────────────┘  │
│                                     │     │                              │
│  /home/hesed/shm/aeron              │     │  /home/hesed/shm/aeron       │
│  /home/hesed/shm/aeron-archive      │     │  (separate aeron dir)        │
└─────────────────────────────────────┘     └──────────────────────────────┘
         │                                              ▲
         │        Multicast: 224.0.1.1:40456           │
         └──────────────────────────────────────────────┘
                   Archive Control: Publisher:8010
                   Replay: Subscriber:40457
```

## Key Changes

### 1. Publisher Changes

**Current State:**
- Connects to external ArchivingMediaDriver via shared memory
- No embedded MediaDriver

**Target State:**
- Runs embedded C++ MediaDriver in-process
- Still connects to external Java ArchivingMediaDriver for archive operations
- Uses two MediaDriver connections:
  - Local embedded driver for publication
  - Java ArchivingMediaDriver for recording control

**Rationale:**
- Publisher needs archive recording capabilities (only available in Java)
- Embedded MediaDriver provides better isolation and control
- Can run Publisher and Java Archive on same server

### 2. Subscriber Changes

**Current State:**
- Optional embedded MediaDriver (already implemented)
- Uses ReplayToLiveHandler for replay-to-live transition
- Manual state management for REPLAY → TRANSITIONING → LIVE

**Target State:**
- Mandatory embedded MediaDriver
- Uses AeronArchive::startReplayMerge() API
- Automatic replay-to-live merging handled by Aeron Archive

**Rationale:**
- ReplayMerge is more robust than manual state machine
- Handles edge cases automatically (overlapping messages, gaps)
- Better performance - Archive manages the merge internally

### 3. ReplayMerge Implementation

**Current ReplayToLiveHandler Issues:**
- Manual detection of replay completion via `imageCount() == 0`
- Potential message loss during transition
- Requires careful timing of live subscription creation
- Complex state management code

**ReplayMerge Benefits:**
- Single subscription receives both replay and live messages
- Archive automatically merges streams based on position
- No message loss or duplication
- Simpler application code

**Implementation:**
```cpp
// Replace this:
replay_to_live_handler_->startReplay(channel, streamId, position);
replay_to_live_handler_->startLive(channel, streamId);
// Manual polling and state management

// With this:
int64_t replaySessionId = archive_->startReplayMerge(
    subscription_id,
    recording_id,
    start_position,
    INT64_MAX,  // replay to end
    live_destination,
    fragment_limit
);
// Automatic merging, single subscription
```

## Technical Issues and Solutions

### Issue 1: Embedded MediaDriver in Publisher

**Problem:** Publisher needs both embedded MediaDriver AND access to Java ArchivingMediaDriver for recording.

**Solution:**
- Publisher runs embedded MediaDriver for local publication
- Connects to separate Java ArchivingMediaDriver via network (localhost:8010)
- Java ArchivingMediaDriver runs as separate process on same server
- Archive control uses UDP channels, not shared memory

**Code Changes:**
```cpp
// Publisher initialization
bool AeronPublisher::initialize() {
    // 1. Start embedded MediaDriver
    startEmbeddedDriver();

    // 2. Connect to local embedded driver
    context_->aeronDir(config_.aeron_dir);
    aeron_ = Aeron::connect(*context_);

    // 3. Create publication on embedded driver
    publication_ = aeron_->addPublication(...);

    // 4. Connect to Java Archive via UDP control channel
    archive_context_->controlRequestChannel("aeron:udp?endpoint=localhost:8010");
    archive_ = AeronArchive::connect(*archive_context_);
}
```

**Configuration:**
- Publisher aeron dir: `/home/hesed/shm/aeron-publisher`
- Archive aeron dir: `/home/hesed/shm/aeron` (Java ArchivingMediaDriver)
- Archive control: UDP `localhost:8010` (not shared memory)

### Issue 2: Archive Directory Separation

**Problem:** Publisher embedded driver and Java ArchivingMediaDriver cannot share same aeron directory.

**Solution:**
- Publisher: `/home/hesed/shm/aeron-publisher` (embedded driver)
- Archive: `/home/hesed/shm/aeron` (Java ArchivingMediaDriver)
- Communication via UDP control channels

### Issue 3: Recording Publications from Different MediaDriver

**Problem:** Java Archive cannot record publications from Publisher's embedded driver (different aeron directories).

**Solution:**
- Publisher creates publication on Java Archive's MediaDriver, not embedded driver
- Publisher connects to TWO Aeron instances:
  - Embedded driver: for local operations (optional)
  - Archive driver: for publication that will be recorded

**Revised Approach:**
```cpp
// Better approach: Don't use embedded driver in Publisher
// Connect directly to Java ArchivingMediaDriver's aeron directory
context_->aeronDir("/home/hesed/shm/aeron");  // Archive's aeron dir
aeron_ = Aeron::connect(*context_);
publication_ = aeron_->addPublication(...);

// Archive can now record this publication
archive_->startRecording(...);
```

**Conclusion:** Publisher should NOT use embedded MediaDriver. It must connect to Java ArchivingMediaDriver's shared memory to create recordable publications.

### Issue 4: ReplayMerge API Complexity

**Problem:** ReplayMerge requires careful parameter setup and subscription management.

**Solution:**
```cpp
// 1. Create subscription first
int64_t subscription_id = aeron_->addSubscription(
    live_channel,
    live_stream_id
);

// 2. Wait for subscription to be available
auto subscription = aeron_->findSubscription(subscription_id);

// 3. Start ReplayMerge (Archive will merge replay + live)
std::string live_destination = "aeron:udp?endpoint=localhost:40457";
int64_t merge_session_id = archive_->startReplayMerge(
    subscription_id,           // The subscription to merge into
    recording_id,              // Which recording to replay
    start_position,            // Start position in recording
    INT64_MAX,                 // Replay length (to end)
    live_destination,          // Where to receive live messages
    fragment_limit             // Messages per poll
);

// 4. Poll single subscription (gets both replay and live)
int fragments_read = subscription->poll(handler, fragment_limit);
```

## Implementation Plan

### Phase 1: Publisher Architecture Decision

**Decision:** Publisher should NOT use embedded MediaDriver.

**Reasoning:**
- Java ArchivingMediaDriver must be able to record Publisher's publications
- Recording only works when publication is on same MediaDriver
- Publisher must use Archive's aeron directory: `/home/hesed/shm/aeron`
- Embedded driver would create isolation preventing recording

**Implementation:**
- Remove embedded MediaDriver from Publisher
- Publisher connects to Java ArchivingMediaDriver via shared memory
- Keep Java ArchivingMediaDriver as separate process on Publisher server

### Phase 2: Subscriber Embedded MediaDriver

**Implementation:**
- Make embedded MediaDriver mandatory (remove optional flag)
- Subscriber uses separate aeron directory: `/home/hesed/shm/aeron-subscriber`
- Connects to Publisher's Archive via UDP control channel

### Phase 3: ReplayMerge Implementation

**Files to Modify:**
- `subscriber/include/AeronSubscriber.h` - Remove ReplayToLiveHandler
- `subscriber/src/AeronSubscriber.cpp` - Implement ReplayMerge
- Remove `subscriber/include/ReplayToLiveHandler.h`
- Remove `subscriber/src/ReplayToLiveHandler.cpp`

**New Methods:**
```cpp
class AeronSubscriber {
    bool startReplayMerge(int64_t recording_id, int64_t start_position);
    // Simplified interface - no manual state management
};
```

### Phase 4: Configuration Updates

**Publisher Config:**
```ini
[aeron]
# Use Java ArchivingMediaDriver's directory (NOT embedded)
dir = /home/hesed/shm/aeron

[archive]
# Archive control (same MediaDriver)
control_request_channel = aeron:udp?endpoint=localhost:8010
```

**Subscriber Config:**
```ini
[aeron]
# Use embedded MediaDriver with separate directory
dir = /home/hesed/shm/aeron-subscriber
use_embedded_driver = true  # Always enabled

[archive]
# Connect to Publisher's Archive via network
control_request_channel = aeron:udp?endpoint=PUBLISHER_IP:8010
```

## Testing Plan

1. **Publisher Test:**
   - Start Java ArchivingMediaDriver
   - Start Publisher (connects to Archive's shared memory)
   - Verify publication creation
   - Verify recording start/stop

2. **Subscriber Test:**
   - Start Subscriber with embedded driver
   - Verify MediaDriver startup
   - Test live subscription
   - Test ReplayMerge from position 0

3. **Integration Test:**
   - Full publisher → archive → replay merge flow
   - Verify zero message loss during merge
   - Check latency statistics

## Migration Checklist

- [x] Analyze current architecture
- [x] Document issues with embedded MediaDriver
- [ ] Remove embedded MediaDriver from Publisher plan
- [ ] Make Subscriber embedded MediaDriver mandatory
- [ ] Implement ReplayMerge in Subscriber
- [ ] Remove ReplayToLiveHandler files
- [ ] Update configuration files
- [ ] Update CLAUDE.md documentation
- [ ] Test Publisher with Archive
- [ ] Test Subscriber with ReplayMerge
- [ ] Integration testing

## Breaking Changes

1. **Publisher:** Must run on same server as Java ArchivingMediaDriver (or use network shared storage)
2. **Subscriber:** Embedded MediaDriver is now required (not optional)
3. **API Change:** `startReplay()` method signature changed to use ReplayMerge
4. **Configuration:** Subscriber must use separate aeron directory

## Backward Compatibility

None. This is a breaking architectural change requiring:
- Reconfiguration of both Publisher and Subscriber
- Rebuild of all binaries
- Updated deployment scripts
