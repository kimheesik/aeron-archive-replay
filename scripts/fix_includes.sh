#!/bin/bash

PROJECT_DIR="/home/hesed/devel/aeron"

echo "Fixing include paths..."

# publisher 헤더 파일들
sed -i 's|#include "wrapper/client/archive/AeronArchive.h"|#include "client/AeronArchive.h"|g' \
    ${PROJECT_DIR}/publisher/include/RecordingController.h

sed -i 's|#include "wrapper/client/archive/AeronArchive.h"|#include "client/AeronArchive.h"|g' \
    ${PROJECT_DIR}/publisher/include/AeronPublisher.h

# subscriber 헤더 파일들
sed -i 's|#include "wrapper/client/archive/AeronArchive.h"|#include "client/AeronArchive.h"|g' \
    ${PROJECT_DIR}/subscriber/include/ReplayToLiveHandler.h

sed -i 's|#include "wrapper/client/archive/AeronArchive.h"|#include "client/AeronArchive.h"|g' \
    ${PROJECT_DIR}/subscriber/include/AeronSubscriber.h

echo "Done! All includes fixed."
echo ""
echo "Modified files:"
echo "  - publisher/include/RecordingController.h"
echo "  - publisher/include/AeronPublisher.h"
echo "  - subscriber/include/ReplayToLiveHandler.h"
echo "  - subscriber/include/AeronSubscriber.h"
