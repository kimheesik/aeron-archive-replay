add_executable(aeron_subscriber
    src/AeronSubscriber.cpp
    src/CheckpointManager.cpp
    src/main.cpp
)

target_include_directories(aeron_subscriber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aeron_subscriber
    aeron_common
    aeron_client
    aeron_archive_client
    pthread
)

# Monitoring example (separate executable for testing)
add_executable(aeron_subscriber_monitored
    src/AeronSubscriber.cpp
    src/CheckpointManager.cpp
    src/subscriber_monitoring_example.cpp
)

target_include_directories(aeron_subscriber_monitored PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aeron_subscriber_monitored
    aeron_common
    aeron_client
    aeron_archive_client
    pthread
)

# Zero-copy example (high-performance architecture)
add_executable(aeron_subscriber_zerocopy
    src/AeronSubscriber.cpp
    src/CheckpointManager.cpp
    src/MessageWorker.cpp
    src/subscriber_zerocopy_example.cpp
)

target_include_directories(aeron_subscriber_zerocopy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aeron_subscriber_zerocopy
    aeron_common
    aeron_client
    aeron_archive_client
    pthread
)
