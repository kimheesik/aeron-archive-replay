#!/bin/bash
# Simple test publisher script

cd /home/hesed/devel/aeron/build

# Start publisher and send start command
(
  echo "start"
  sleep 30  # Send messages for 30 seconds
  echo "stop"
  sleep 2
  echo "quit"
) | ./publisher/aeron_publisher --config ../config/aeron-local.ini --interval 10