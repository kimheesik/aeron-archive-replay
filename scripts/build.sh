#!/bin/bash

PROJECT_DIR="/home/hesed/devel/aeron"
BUILD_DIR="${PROJECT_DIR}/build"

echo "Building Aeron Example Project..."

# 빌드 디렉토리 생성
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

# CMake 실행
cmake .. -DCMAKE_BUILD_TYPE=Release

# 빌드
make -j$(nproc)

echo "Build complete!"
echo "Executables:"
echo "  - ${BUILD_DIR}/publisher/aeron_publisher"
echo "  - ${BUILD_DIR}/subscriber/aeron_subscriber"
