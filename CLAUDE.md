# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
- Uses `/dev/shm/aeron` for Aeron shared memory (configured in script)
- Archive recordings stored in `/home/hesed/aeron-archive`
- Control channel: `localhost:8010`
- Must be running before starting Publisher or Subscriber

**Note**: There's a discrepancy - the start script uses `/dev/shm/aeron` but `AeronConfig.h` has `/home/hesed/shm/aeron`. When modifying configurations, ensure both match.

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

**Live mode** (receive real-time messages):
```bash
cd /home/hesed/devel/aeron/build
./subscriber/aeron_subscriber
```

**Replay mode** (replay from position 0, then transition to live):
```bash
./subscriber/aeron_subscriber --replay 0
```

## Key Architecture Concepts

### Replay-to-Live Mechanism

The `ReplayToLiveHandler` class implements a sophisticated state machine with three modes:
- **REPLAY**: Receiving historical data from archive
- **TRANSITIONING**: Brief transition state when replay completes
- **LIVE**: Receiving real-time data from publisher

**Critical Implementation Detail**: The handler pre-creates the live subscription BEFORE replay completes to ensure seamless transition without message loss. Replay completion is detected by checking `replay_subscription_->imageCount() == 0`.

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

This is the **single source of truth** for all endpoint configurations. Before running the system:

1. Verify `AERON_DIR` matches the ArchivingMediaDriver script setting
2. Currently set to `/home/hesed/shm/aeron` in code
3. `start_archive_driver.sh` uses `/dev/shm/aeron`
4. **These should match** - fix one or the other before deployment

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
2. Verify `/dev/shm/aeron` (or `/home/hesed/shm/aeron`) exists
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

1. **Aeron directory location**: Currently configured for `/home/hesed/shm/aeron` (regular disk). For better performance, use `/dev/shm/aeron` (tmpfs/shared memory).

2. **Transport mechanism**: Currently using UDP localhost. For even lower latency, consider IPC transport.

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
