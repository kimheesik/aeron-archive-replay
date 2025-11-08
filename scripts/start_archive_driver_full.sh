#!/bin/bash

AERON_JAR="/home/hesed/aeron/bin/aeron-all-1.50.0-SNAPSHOT.jar"
AERON_DIR="/dev/shm/aeron"
ARCHIVE_DIR="/home/hesed/aeron-archive"

echo "=========================================="
echo "Starting Aeron ArchivingMediaDriver"
echo "=========================================="
echo "Aeron Directory: ${AERON_DIR}"
echo "Archive Directory: ${ARCHIVE_DIR}"
echo ""

# Archive 디렉토리 생성
mkdir -p ${ARCHIVE_DIR}

# ArchivingMediaDriver 실행
java -cp ${AERON_JAR} \
  -XX:+UseG1GC \
  -XX:MaxGCPauseMillis=10 \
  -Xmx2g \
  -Daeron.dir=${AERON_DIR} \
  -Daeron.archive.dir=${ARCHIVE_DIR} \
  -Daeron.archive.control.channel=aeron:udp?endpoint=localhost:8010 \
  -Daeron.archive.control.response.channel=aeron:udp?endpoint=localhost:0 \
  -Daeron.archive.recording.events.channel=aeron:udp?endpoint=localhost:8011 \
  -Daeron.archive.replication.channel=aeron:udp?endpoint=localhost:8012 \
  -Daeron.threading.mode=SHARED \
  -Daeron.archive.threading.mode=SHARED \
  -Daeron.term.buffer.sparse.file=false \
  -Daeron.pre.touch.mapped.memory=true \
  -Daeron.socket.so_sndbuf=2097152 \
  -Daeron.socket.so_rcvbuf=2097152 \
  -Daeron.rcv.initial.window.length=2097152 \
  -Daeron.mtu.length=8192 \
  -Daeron.ipc.mtu.length=8192 \
  -Daeron.archive.file.sync.level=0 \
  -Daeron.archive.catalog.file.sync.level=0 \
  -Daeron.spies.simulate.connection=true \
  io.aeron.archive.ArchivingMediaDriver

echo ""
echo "ArchivingMediaDriver stopped"
