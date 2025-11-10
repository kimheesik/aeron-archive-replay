#!/bin/bash

# Aeron Latency Measurement Script

BUILD_DIR="/home/hesed/devel/aeron/build"
AERON_DIR="/home/hesed/shm/aeron"
OUTPUT_FILE="/tmp/aeron_latency.log"

# 색상 코드
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Aeron Latency Test${NC}"
echo -e "${BLUE}========================================${NC}"

# 측정 샘플 수
SAMPLE_COUNT=${1:-1000}
echo "Measuring latency for $SAMPLE_COUNT messages..."

# 이전 로그 제거
rm -f $OUTPUT_FILE

# Publisher 시작
echo -e "\n${YELLOW}Starting Publisher...${NC}"
$BUILD_DIR/publisher/aeron_publisher \
    --aeron-dir $AERON_DIR \
    --interval 10 > /dev/null 2>&1 &
PUBLISHER_PID=$!
sleep 2

# Subscriber 시작 및 지연시간 측정
echo -e "${YELLOW}Starting Subscriber and measuring latency...${NC}"
$BUILD_DIR/subscriber/aeron_subscriber 2>&1 | \
awk -v sample_count=$SAMPLE_COUNT '
BEGIN {
    count = 0;
    sum = 0;
    min = 0;
    max = 0;
}
/Received message/ {
    # 메시지에서 전송 타임스탬프 추출
    match($0, /at ([0-9]+)/, arr);
    send_time = arr[1];

    # 현재 시간 (nanoseconds)
    cmd = "date +%s%N";
    cmd | getline recv_time;
    close(cmd);

    # 지연시간 계산 (microseconds)
    latency = (recv_time - send_time) / 1000;

    # 통계 계산
    sum += latency;
    count++;

    if (min == 0 || latency < min) min = latency;
    if (latency > max) max = latency;

    # 배열에 저장 (백분위수 계산용)
    latencies[count] = latency;

    # 진행 표시
    if (count % 100 == 0) {
        avg = sum / count;
        printf "\rProgress: %d/%d (Avg: %.2f μs)", count, sample_count, avg;
    }

    # 목표 샘플 수 도달
    if (count >= sample_count) {
        printf "\n";

        # 정렬 (백분위수 계산용)
        asort(latencies, sorted);

        # 통계 출력
        avg = sum / count;
        p50_idx = int(count * 0.50);
        p95_idx = int(count * 0.95);
        p99_idx = int(count * 0.99);

        printf "\n========================================\n";
        printf "Latency Statistics (%d samples)\n", count;
        printf "========================================\n";
        printf "Min:        %10.2f μs\n", min;
        printf "Max:        %10.2f μs\n", max;
        printf "Average:    %10.2f μs\n", avg;
        printf "Median(P50):%10.2f μs\n", sorted[p50_idx];
        printf "P95:        %10.2f μs\n", sorted[p95_idx];
        printf "P99:        %10.2f μs\n", sorted[p99_idx];
        printf "========================================\n";

        # 지연시간 분포
        printf "\nLatency Distribution:\n";
        ranges[0] = 0; range_names[0] = "   0-100 μs";
        ranges[1] = 100; range_names[1] = " 100-500 μs";
        ranges[2] = 500; range_names[2] = "500-1000 μs";
        ranges[3] = 1000; range_names[3] = "  1-5 ms";
        ranges[4] = 5000; range_names[4] = "  5-10 ms";
        ranges[5] = 10000; range_names[5] = "   >10 ms";

        for (i = 1; i <= count; i++) {
            lat = sorted[i];
            if (lat < 100) dist[0]++;
            else if (lat < 500) dist[1]++;
            else if (lat < 1000) dist[2]++;
            else if (lat < 5000) dist[3]++;
            else if (lat < 10000) dist[4]++;
            else dist[5]++;
        }

        for (i = 0; i < 6; i++) {
            pct = (dist[i] / count) * 100;
            bar = "";
            for (j = 0; j < int(pct/2); j++) bar = bar "#";
            printf "%s: %6.2f%% %s\n", range_names[i], pct, bar;
        }

        exit 0;
    }
}
' &

AWK_PID=$!

# 완료 대기
wait $AWK_PID 2>/dev/null

# Publisher 종료
kill $PUBLISHER_PID 2>/dev/null
wait $PUBLISHER_PID 2>/dev/null

echo -e "\n${GREEN}Latency measurement completed${NC}"
