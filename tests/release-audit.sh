#!/bin/sh
# Проверки сохранности данных: ненулевой код означает открытые релизные дефекты.
set -eu
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD:/workspace:ro" -w /workspace \
    aurora-build-tools-nighteugene:5.2.1.200 sh -c '
    moc app/src/storage.h -o /tmp/moc_storage.cpp &&
    g++ -std=c++17 -fPIC -no-pie -fsanitize=undefined -fno-sanitize-recover=all \
        -I/workspace -I/workspace/app/src tests/release_audit.cpp \
        app/src/storage.cpp /tmp/moc_storage.cpp app/src/xiaomi/activityparser.cpp \
        app/src/xiaomi/crypto.cpp -lcrypto -o /tmp/release-audit \
        $(pkg-config --cflags --libs Qt5Core Qt5Sql) && /tmp/release-audit &&
    moc app/src/xiaomi/activityfetcher.h -o /tmp/moc_activityfetcher.cpp &&
    g++ -std=c++17 -fPIC -no-pie -fsanitize=undefined -fno-sanitize-recover=all \
        -I/workspace -I/workspace/app/src/xiaomi tests/activityfetcher_test.cpp \
        app/src/xiaomi/activityfetcher.cpp app/src/xiaomi/activityparser.cpp \
        /tmp/moc_activityfetcher.cpp -o /tmp/activityfetcher-test \
        $(pkg-config --cflags --libs Qt5Core) && /tmp/activityfetcher-test'
