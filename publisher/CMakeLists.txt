add_executable(aeron_publisher
    src/AeronPublisher.cpp
    src/RecordingController.cpp
    src/main.cpp
)

target_include_directories(aeron_publisher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(aeron_publisher
    aeron_common
    aeron_client
    aeron_archive_client
    pthread
)
