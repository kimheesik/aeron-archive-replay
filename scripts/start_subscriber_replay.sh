#!/bin/bash

BUILD_DIR="/home/hesed/devel/aeron/build"

if [ -z "$1" ]; then
    echo "Usage: $0 <start_position>"
    echo "Example: $0 0"
    exit 1
fi

START_POSITION=$1

echo "Starting Aeron Subscriber (REPLAY mode from position: ${START_POSITION})..."
${BUILD_DIR}/subscriber/aeron_subscriber --replay ${START_POSITION}#!/bin/bash

BUILD_DIR="/home/hesed/devel/aeron/build"

if [ -z "$1" ]; then
    echo "Usage: $0 <start_position>"
    echo "Example: $0 0"
    exit 1
fi

START_POSITION=$1

echo "Starting Aeron Subscriber (REPLAY mode from position: ${START_POSITION})..."
${BUILD_DIR}/subscriber/aeron_subscriber --replay ${START_POSITION}
