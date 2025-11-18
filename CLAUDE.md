# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Reference (Fast Project Overview)

**Project Home**: `/home/hesed/devel/aeron`

**What is this?**: Aeron-based C++ Publisher/Subscriber messaging system with Recording/Replay (v1.50.1)

**Architecture**: 3-Process System
- `ArchivingMediaDriver` (Java) - Archive recording/replay service
- `aeron_publisher` (C++) - Message publisher with recording control
- `aeron_subscriber` (C++) - Message subscriber with replay-to-live transition

**Code Statistics**:
- Total C++ lines: ~1,831 (480 headers + 1,351 source)
- 3 modules: `common/`, `publisher/`, `subscriber/`
- Build system: CMake + C++17

**Key Files**:
```
common/include/AeronConfig.h            - Central configuration (channels, stream IDs)
common/src/ConfigLoader.cpp             - INI config loader (405 lines)
publisher/src/AeronPublisher.cpp        - Message publisher (226 lines)
publisher/src/RecordingController.cpp   - Archive recording control (228 lines)
subscriber/src/AeronSubscriber.cpp      - Message receiver + latency measurement (337 lines)
subscriber/include/SPSCQueue.h          - Lock-free queue for monitoring (NEW)
subscriber_monitoring_example.cpp       - Monitoring example with stats output (NEW)
config/*.ini                            - Runtime configurations (7 files)
scripts/start_archive_driver.sh         - Start Java ArchivingMediaDriver
```

**Quick Build & Run**:
```bash
# Build (standard)
cd /home/hesed/devel/aeron/build && make -j$(nproc)

# Build with monitoring
cd /home/hesed/devel/aeron/build && make aeron_subscriber_monitored

# Run (3 terminals)
# Terminal 1: scripts/start_archive_driver.sh
# Terminal 2: build/publisher/aeron_publisher --config config/aeron-local.ini
# Terminal 3: build/subscriber/aeron_subscriber [--replay-auto]
# Terminal 3 (monitoring): build/subscriber/aeron_subscriber_monitored [--replay-auto]
```

**Key Concepts**:
- **ReplayMerge**: Simplified replay-to-live transition using Archive's startReplay API
- **Auto-Discovery**: Automatic recording ID discovery (no manual lookup required)
- **Gap Detection**: Real-time message loss detection and reporting
- **Embedded MediaDriver**: Subscriber ALWAYS uses embedded MediaDriver (mandatory)
- **Internal Latency Measurement**: Nanosecond precision inside subscriber (NOT external scripts)
- **Lock-free Monitoring**: SPSC queue-based monitoring with 0.009% overhead (NEW)
- **Message Format**: `"Message <N> at <nanosecond_timestamp>"`
- **Configuration Hierarchy**: `AeronConfig.h` → INI files → CLI args
- **Performance (WSL2)**: ~74μs min, ~1.2ms avg, ~2.5ms max latency

**Dependencies**:
- Aeron SDK: `/home/hesed/aeron/` (include + lib)
- Java ArchivingMediaDriver: Runs separately, controls `/home/hesed/shm/aeron`
- CMake 3.15+, C++17, pthread

**Common Issues**:
- Must start ArchivingMediaDriver FIRST before publisher/subscriber
- Ensure `/home/hesed/shm/aeron` directory exists and is writable

---

## Project Overview

This is an Aeron-based high-performance messaging system implementing Publisher/Subscriber architecture with Recording/Replay capabilities. The system consists of C++ Publisher/Subscriber applications communicating via Aeron (v1.50.1) with a Java ArchivingMediaDriver for stream recording and replay functionality.

**Key Architecture**: Publisher and Subscriber are separate C++ applications that communicate through Aeron's shared memory transport. The ArchivingMediaDriver (Java) runs as a separate process and provides recording/replay services.

## Build System

### Building the Project

```bash
cd /home/hesed/devel/aeron/build
cmake ..
make -j$(nproc)
```

Or use the convenience script:
```bash
cd /home/hesed/devel/aeron/scripts
./build.sh
```

**Important Build Notes**:
- Aeron SDK is installed at `/home/hesed/aeron/` (not a system installation)
- CMake is configured to use `/home/hesed/aeron/include` and `/home/hesed/aeron/lib`
- Requires C++17 and CMake 3.15+
- Build outputs: `build/publisher/aeron_publisher` and `build/subscriber/aeron_subscriber`

### Rebuild After Changes

After modifying source files, rebuild from the build directory:
```bash
cd /home/hesed/devel/aeron/build
make -j$(nproc)
```

## Running the System

### Critical: Three-Process System

The system requires THREE separate processes running simultaneously:

1. **ArchivingMediaDriver** (Java - must start first)
2. **Publisher** (C++)
3. **Subscriber** (C++)

### Starting ArchivingMediaDriver

**ALWAYS start this first** in a dedicated terminal:
```bash
cd /home/hesed/devel/aeron/scripts
./start_archive_driver.sh
```

**Configuration**:
- Uses `/home/hesed/shm/aeron` for Aeron shared memory (avoids /dev/shm size limits)
- Archive recordings stored in `/home/hesed/shm/aeron-archive`
- Control channel: `localhost:8010`
- Must be running before starting Publisher or Subscriber

**Note**: All configurations now consistently use `/home/hesed/shm/aeron` instead of `/dev/shm/aeron` to avoid tmpfs size limitations.

### Starting Publisher

```bash
cd /home/hesed/devel/aeron/build
./publisher/aeron_publisher
```

Interactive commands:
- `start` - Start recording the stream
- `stop` - Stop recording
- `quit` - Exit publisher

### Starting Subscriber

**IMPORTANT**: Subscriber now ALWAYS uses embedded MediaDriver (mandatory).

**Live mode** (receive real-time messages):
```bash
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber --config ../config/aeron-local.ini
```

**ReplayMerge mode with auto-discovery** (finds latest recording automatically):
```bash
./subscriber/aeron_subscriber --config ../config/aeron-local.ini --replay-auto
```

**ReplayMerge mode with specific recording ID**:
```bash
./subscriber/aeron_subscriber --config ../config/aeron-local.ini --replay-merge 1 --position 0
```

**Advanced**: Auto-discovery with custom start position:
```bash
./subscriber/aeron_subscriber --config ../config/aeron-local.ini --replay-auto --position 1000
```

## Key Architecture Concepts

### ReplayMerge Mechanism

The Subscriber implements a simplified ReplayMerge using Archive's `startReplay()` API:
- **Create live subscription** on the live channel (e.g., multicast)
- **Start replay** to a separate replay destination channel
- **Single subscription** receives both replay and live messages
- **Automatic transition** when replay completes

**Implementation** (`AeronSubscriber::startReplayMerge()`):
```cpp
// 1. Create live subscription
subscription_ = aeron_->addSubscription(live_channel, stream_id);

// 2. Start replay to replay destination
replay_session_id = archive_->startReplay(
    recordingId, startPosition, INT64_MAX,
    replay_destination, stream_id
);

// 3. Poll single subscription for both replay and live messages
subscription_->poll(handler, fragmentLimit);
```

**Note**: This is a simplified implementation. Replay messages arrive on `replay_destination`, then automatically transitions to live messages from `live_channel` when replay completes.

### Latency Measurement

**IMPORTANT**: Latency is measured INTERNALLY in the Subscriber, not externally via shell scripts.

The internal measurement implementation (`AeronSubscriber.cpp:86-136`):
1. Records receive timestamp IMMEDIATELY upon entering `handleMessage()`
2. Parses send timestamp from message payload (`"Message N at <timestamp>"`)
3. Calculates latency in microseconds: `(recv_timestamp - send_timestamp) / 1000.0`
4. Collects min/max/average statistics
5. Prints statistics every 1000 messages

**Do NOT use external measurement** (like `date +%s%N` in bash scripts) as this adds ~10ms overhead per measurement.

### Message Format

Publisher sends messages in format: `"Message <counter> at <nanosecond_timestamp>"`

The timestamp is used by Subscriber for internal latency calculation.

### Channel Configuration

All channel/port configurations are centralized in `common/include/AeronConfig.h`:
- Publication/Subscription: UDP `localhost:40456` (stream ID 10)
- Replay: UDP `localhost:40457` (stream ID 20)
- Archive Control: `localhost:8010`

**When changing channels**: Update `AeronConfig.h` and rebuild all components.

## Common Development Tasks

### Modifying Latency Reporting Frequency

Edit `subscriber/src/AeronSubscriber.cpp` line 121:
```cpp
if (message_count_ % 1000 == 0) {  // Change 1000 to desired frequency
    printLatencyStats();
}
```

### Adjusting Publisher Message Rate

Publisher sends messages based on interval parameter. Default is 100ms between messages. To change:
```bash
./publisher/aeron_publisher --interval 10  # 10ms between messages
```

### Performance Testing Scripts

Two test scripts are available:

**Latency Test** (measures end-to-end latency):
```bash
cd /home/hesed/devel/aeron/scripts
./latency_test.sh 1000  # Test with 1000 samples
```

**Performance Test** (measures throughput and message loss):
```bash
./performance_test.sh
```

**Note**: These scripts use external measurement and have measurement overhead. For accurate latency, check the Subscriber's internal statistics output.

## Configuration Files

### AeronConfig.h

This is the **single source of truth** for all endpoint configurations.

**AERON_DIR**: Set to `/home/hesed/shm/aeron` (consistent across all configs)
- Avoids `/dev/shm` size limitations in WSL2/containerized environments
- All INI config files and scripts now use `/home/hesed/shm/*` paths
- Can be overridden via INI files or CLI arguments

### CMake Include Paths

The build system requires specific Aeron paths:
```cmake
set(AERON_INCLUDE_DIR "/home/hesed/aeron/include")
set(AERON_LIB_DIR "/home/hesed/aeron/lib")
```

These paths are hard-coded and must match the actual Aeron installation location.

## Troubleshooting

### "Failed to connect to Archive"

**Cause**: ArchivingMediaDriver not running or wrong control channel.

**Solution**:
1. Check if Java process is running: `ps aux | grep ArchivingMediaDriver`
2. Verify `/home/hesed/shm/aeron` directory exists and is writable
3. Restart ArchivingMediaDriver first
4. Ensure control channel in `AeronConfig.h` matches driver configuration

### "No recording found"

**Cause**: No recording data exists in archive.

**Solution**:
1. Start Publisher
2. Type `start` to begin recording
3. Wait a few seconds for messages to be recorded
4. Type `stop` to end recording
5. Verify files exist in `/home/hesed/aeron-archive/`

### Replay Not Transitioning to Live

**Cause**: Live subscription not properly created or replay not detecting completion.

**Debug**: Check `ReplayToLiveHandler::checkTransitionToLive()` in `subscriber/src/ReplayToLiveHandler.cpp`. Add logging:
```cpp
std::cout << "Replay image count: " << replay_subscription_->imageCount() << std::endl;
```

### Build Errors: "client/AeronArchive.h: No such file"

**Cause**: Incorrect include path configuration.

**Solution**:
1. Verify `/home/hesed/aeron/include/client/AeronArchive.h` exists
2. Check `CMakeLists.txt` include directories
3. Clean rebuild: `cd build && rm -rf * && cmake .. && make`

## Performance Considerations

### Current Performance Characteristics

Based on internal measurements (WSL2 environment):
- Min latency: ~34 μs
- Average latency: ~3.3 ms (3,300 μs)
- Max latency: ~42 ms

### Performance Bottlenecks

1. **Aeron directory location**: Currently configured for `/home/hesed/shm/aeron` (regular disk). This avoids `/dev/shm` size limits but may have slightly higher latency than tmpfs. For maximum performance with sufficient space, consider `/dev/shm/aeron`.

2. **Transport mechanism**: Currently using UDP localhost. For even lower latency, consider IPC transport (requires shared aeron directory).

3. **WSL2 overhead**: Running on WSL2 adds virtualization overhead. Native Linux would be faster.

4. **Logging overhead**: Subscriber prints statistics every 1000 messages. Reduce logging frequency for maximum throughput.

### Reducing Logging Overhead

To minimize logging impact on latency measurements:
- Increase reporting interval in `AeronSubscriber.cpp` (line 121)
- Redirect stdout to `/dev/null` for throughput tests
- Consider conditional compilation flags for debug vs. production builds

## Code Organization

### Module Structure

```
common/        - Shared configuration and utilities
  ├── AeronConfig.h    - All channel/endpoint constants
  └── Logger.h         - Logging utilities

publisher/     - Publisher application
  ├── AeronPublisher.h/cpp         - Main publisher logic
  ├── RecordingController.h/cpp    - Archive recording control
  └── main.cpp                      - Entry point with CLI

subscriber/    - Subscriber application
  ├── AeronSubscriber.h/cpp         - Main subscriber logic with latency measurement
  ├── ReplayToLiveHandler.h/cpp     - Replay-to-live state machine
  └── main.cpp                       - Entry point with CLI argument parsing
```

### Threading Model

- Publisher: Single-threaded with sleep intervals between messages
- Subscriber: Single-threaded polling loop with `fragmentLimit=10`
- ArchivingMediaDriver: Uses `SHARED` threading mode (one thread for media driver and archive)

For higher throughput, consider DEDICATED threading mode in ArchivingMediaDriver.

## Testing

### Manual Test Procedure

1. Terminal 1: Start ArchivingMediaDriver
2. Terminal 2: Start Publisher → type `start` → wait 10 seconds → type `stop`
3. Terminal 3: Start Subscriber in replay mode (`--replay 0`)
4. Verify messages transition from `[REPLAY]` to `[LIVE]` tags
5. Confirm no message loss across the transition

### Automated Test Scripts

The `latency_test.sh` and `performance_test.sh` scripts automate testing but use external measurement. For production latency metrics, rely on the Subscriber's internal statistics output.

## Subscriber Monitoring Feature

### Overview

The subscriber now includes a lock-free monitoring system that provides real-time statistics without impacting message reception performance.

### Key Components

**SPSCQueue.h** (`subscriber/include/SPSCQueue.h`)
- Lock-free Single Producer Single Consumer ring buffer
- 16K capacity (configurable)
- ~50ns enqueue/dequeue operations
- Cache-line aligned to prevent false sharing

**MessageCallback API** (`AeronSubscriber.h`)
```cpp
using MessageCallback = std::function<void(
    int64_t message_number,
    int64_t send_timestamp,
    int64_t recv_timestamp,
    int64_t position
)>;

void setMessageCallback(MessageCallback callback);
```

**Monitoring Example** (`subscriber_monitoring_example.cpp`)
- Complete working example
- Separate monitoring thread
- Statistics output every 100 messages
- Supports both Live and ReplayMerge modes

### Usage

**Build:**
```bash
cd /home/hesed/devel/aeron/build
make aeron_subscriber_monitored
```

**Run (Live mode):**
```bash
./subscriber/aeron_subscriber_monitored
```

**Run (ReplayMerge Auto mode):**
```bash
./subscriber/aeron_subscriber_monitored --replay-auto
```

### Performance

- **Callback overhead**: ~60ns per message
- **Queue enqueue**: ~50ns
- **Total overhead**: 0.009% (negligible)
- **Queue usage**: Typically 0% (processing faster than reception)
- **Message loss**: 0%

### Output Example

```
========================================
📊 모니터링 통계 (최근 100건)
========================================
총 메시지 수:   1000
최근 메시지:    #6704 at position 96000
평균 레이턴시:  1195.12 μs
최소 레이턴시:  74 μs
최대 레이턴시:  2466 μs
Queue 크기:     0 / 16383
Queue 사용률:   0.00%
========================================
```

### Documentation

- **User Guide**: `SUBSCRIBER_MONITORING_GUIDE.md`
- **Design Doc**: `SUBSCRIBER_MONITORING_DESIGN.md`
- **Example Code**: `subscriber_monitoring_example.cpp`

### Integration

To integrate monitoring into your own code:

1. Create `MessageStatsQueue` in main()
2. Start monitoring thread
3. Call `subscriber.setMessageCallback()` with queue enqueue logic
4. Process stats from queue in monitoring thread

See `subscriber_monitoring_example.cpp` for complete implementation.

