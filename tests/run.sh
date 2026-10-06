#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD:/workspace:ro" -w /workspace \
    aurora-build-tools-nighteugene:5.2.1.200 sh -c '
    g++ -std=c++17 -fPIC -no-pie -fsanitize=undefined -fno-sanitize-recover=all \
        -I/workspace tests/proto_test.cpp app/src/xiaomi/crypto.cpp -lcrypto -o /tmp/proto-test \
        $(pkg-config --cflags --libs Qt5Core) && /tmp/proto-test'
