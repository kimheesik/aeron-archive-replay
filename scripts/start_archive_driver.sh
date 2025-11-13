#!/bin/bash

AERON_JAR="/home/hesed/aeron/bin/aeron-all-1.50.0-SNAPSHOT.jar"
AERON_DIR="/home/hesed/shm/aeron"
ARCHIVE_DIR="/home/hesed/shm/aeron-archive"
LOG_FILE="/home/hesed/devel/aeron/logs/archive_driver.log"

echo "=========================================="
echo "Starting Aeron ArchivingMediaDriver"
echo "=========================================="
echo "Aeron Directory: ${AERON_DIR}"
echo "Archive Directory: ${ARCHIVE_DIR}"
echo "Control Channel: aeron:udp?endpoint=localhost:8010"
echo "Recording Events: aeron:udp?endpoint=localhost:8011"
echo "Replication Channel: aeron:udp?endpoint=localhost:8012"
echo "Log File: ${LOG_FILE}"
echo ""

# Archive 디렉토리 생성
mkdir -p ${ARCHIVE_DIR}

# 로그 디렉토리 생성
mkdir -p "$(dirname ${LOG_FILE})"

# Aeron 공유 메모리 디렉토리 정리 (선택사항 - 처음 실행 시에만)
# rm -rf ${AERON_DIR}

# ArchivingMediaDriver 실행 (foreground mode)
echo "Starting ArchivingMediaDriver... (Press Ctrl+C to stop)"
java -cp ${AERON_JAR} \
  --add-opens java.base/jdk.internal.misc=ALL-UNNAMED \
  --add-opens java.base/sun.nio.ch=ALL-UNNAMED \
  -Daeron.dir=${AERON_DIR} \
  -Daeron.archive.dir=${ARCHIVE_DIR} \
  -Daeron.archive.control.channel=aeron:udp?endpoint=localhost:8010 \
  -Daeron.archive.recording.events.channel=aeron:udp?endpoint=localhost:8011 \
  -Daeron.archive.replication.channel=aeron:udp?endpoint=localhost:8012 \
  -Daeron.threading.mode=SHARED \
  -Daeron.archive.threading.mode=SHARED \
  io.aeron.archive.ArchivingMediaDriver 2>&1 | tee ${LOG_FILE} &

echo ""
echo "ArchivingMediaDriver stopped"
