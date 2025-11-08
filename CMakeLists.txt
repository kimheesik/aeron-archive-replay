cmake_minimum_required(VERSION 3.15)
project(AeronExample)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Aeron 경로 (install이 아닌 include 직접 사용)
set(AERON_INCLUDE_DIR "/home/hesed/aeron/include")
set(AERON_LIB_DIR "/home/hesed/aeron/lib")

# Include 디렉토리
include_directories(
    ${AERON_INCLUDE_DIR}              # /home/hesed/aeron/include
    ${AERON_INCLUDE_DIR}/aeron        # /home/hesed/aeron/include/aeron
    ${CMAKE_SOURCE_DIR}/common/include
)
#include_directories(
#    ${AERON_INCLUDE_DIR}
#    ${CMAKE_SOURCE_DIR}/common/include
#)

# 라이브러리 디렉토리
link_directories(${AERON_LIB_DIR})

# 서브디렉토리
add_subdirectory(common)
add_subdirectory(publisher)
add_subdirectory(subscriber)
