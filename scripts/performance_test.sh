#!/bin/bash

# Aeron Performance Test Script

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="/home/hesed/devel/aeron/build"
AERON_DIR="/home/hesed/shm/aeron"
ARCHIVE_DIR="/home/hesed/shm/aeron-archive"

# 테스트 설정
TEST_DURATION=20  # 초
MESSAGE_INTERVAL=10  # ms (100 msg/sec)
OUTPUT_FILE="/tmp/aeron_perf_test.log"

# 색상 코드
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Aeron Performance Test${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "Test Duration: ${TEST_DURATION}s"
echo -e "Message Interval: ${MESSAGE_INTERVAL}ms"
echo -e "Expected Throughput: $((1000 / MESSAGE_INTERVAL)) msg/s"
echo ""

# Archive Driver 확인
echo -e "${YELLOW}Checking Archive Driver...${NC}"
if pgrep -f "ArchivingMediaDriver" > /dev/null; then
    echo -e "${GREEN}✓ Archive Driver is running${NC}"
else
    echo -e "${RED}✗ Archive Driver is NOT running${NC}"
    echo "Please start Archive Driver first:"
    echo "  cd $SCRIPT_DIR && ./start_archive_driver.sh"
    exit 1
fi

# 이전 로그 파일 제거
rm -f $OUTPUT_FILE

# Publisher 시작
echo -e "\n${YELLOW}Starting Publisher...${NC}"
$BUILD_DIR/publisher/aeron_publisher \
    --aeron-dir $AERON_DIR \
    --interval $MESSAGE_INTERVAL > /dev/null 2>&1 &
PUBLISHER_PID=$!

sleep 2

if ! ps -p $PUBLISHER_PID > /dev/null; then
    echo -e "${RED}✗ Failed to start Publisher${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Publisher started (PID: $PUBLISHER_PID)${NC}"

# Subscriber 시작 및 로그 수집
echo -e "\n${YELLOW}Starting Subscriber and collecting data...${NC}"
timeout $TEST_DURATION $BUILD_DIR/subscriber/aeron_subscriber \
    2>&1 | tee $OUTPUT_FILE &
SUBSCRIBER_PID=$!

# 진행 표시
for i in $(seq 1 $TEST_DURATION); do
    echo -ne "\rProgress: [$i/$TEST_DURATION]s"
    sleep 1
done
echo ""

# Subscriber 종료 대기
wait $SUBSCRIBER_PID 2>/dev/null

# Publisher 종료
echo -e "\n${YELLOW}Stopping Publisher...${NC}"
kill $PUBLISHER_PID 2>/dev/null
wait $PUBLISHER_PID 2>/dev/null

# 성능 분석
echo -e "\n${BLUE}========================================${NC}"
echo -e "${BLUE}Performance Results${NC}"
echo -e "${BLUE}========================================${NC}"

# 1. 메시지 수 카운트
TOTAL_MESSAGES=$(grep -c "Received message" $OUTPUT_FILE 2>/dev/null || echo 0)
THROUGHPUT=$(echo "scale=2; $TOTAL_MESSAGES / $TEST_DURATION" | bc)

echo -e "${GREEN}Total Messages Received:${NC} $TOTAL_MESSAGES"
echo -e "${GREEN}Average Throughput:${NC} $THROUGHPUT msg/s"

# 2. 모드별 메시지 수
LIVE_MESSAGES=$(grep -c "\[LIVE\]" $OUTPUT_FILE 2>/dev/null || echo 0)
REPLAY_MESSAGES=$(grep -c "\[REPLAY\]" $OUTPUT_FILE 2>/dev/null || echo 0)

echo -e "${GREEN}Live Messages:${NC} $LIVE_MESSAGES"
echo -e "${GREEN}Replay Messages:${NC} $REPLAY_MESSAGES"

# 3. 메시지 손실률 계산 (간단한 버전)
EXPECTED_MESSAGES=$((1000 / MESSAGE_INTERVAL * TEST_DURATION))
LOSS_RATE=$(echo "scale=2; (1 - $TOTAL_MESSAGES / $EXPECTED_MESSAGES) * 100" | bc)

echo -e "${GREEN}Expected Messages:${NC} $EXPECTED_MESSAGES"
if (( $(echo "$LOSS_RATE < 0" | bc -l) )); then
    echo -e "${YELLOW}Message Loss Rate:${NC} 0.00% (more received than expected)"
else
    echo -e "${YELLOW}Message Loss Rate:${NC} ${LOSS_RATE}%"
fi

# 4. 메시지 간격 분석
echo -e "\n${BLUE}Message Sequence Analysis:${NC}"
grep "Received message #" $OUTPUT_FILE | \
    awk '{print $4}' | \
    sed 's/#//' | \
    awk 'NR>1 {diff=$1-prev; sum+=diff; count++; if(diff>max)max=diff; if(min==0||diff<min)min=diff} {prev=$1} END {
        if(count>0) {
            avg=sum/count;
            printf "  Min interval: %.0f messages\n", min;
            printf "  Max interval: %.0f messages\n", max;
            printf "  Avg interval: %.2f messages\n", avg;
        }
    }'

echo -e "\n${BLUE}========================================${NC}"
echo -e "Log file saved: ${OUTPUT_FILE}"
echo -e "${BLUE}========================================${NC}"

# 상세 로그 옵션
read -p "Do you want to see detailed log? (y/n): " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    less $OUTPUT_FILE
fi
