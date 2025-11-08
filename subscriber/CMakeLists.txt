add_executable(aeron_subscriber
    src/AeronSubscriber.cpp
    src/ReplayToLiveHandler.cpp
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
