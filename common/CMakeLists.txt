# 소스 파일 정의
set(COMMON_SOURCES
    src/Logger.cpp
    src/AeronConfig.cpp
)

# 헤더 파일 정의 (선택사항, 명시적으로 표시)
set(COMMON_HEADERS
    include/Logger.h
    include/AeronConfig.h
)

# Static 라이브러리 생성
add_library(aeron_common STATIC
    ${COMMON_SOURCES}
    ${COMMON_HEADERS}
)

# Include 디렉토리 설정
target_include_directories(aeron_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
