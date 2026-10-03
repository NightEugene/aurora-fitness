#!/bin/sh
# Сборка и (опционально) установка ru.nighteugene.aurorafitness
# через Aurora SDK 5.2.1.200 (docker build tools + apptool).
#
# Использование:
#   ./build.sh           собрать, подписать и провалидировать RPM
#   ./build.sh --deploy  дополнительно установить на устройство
#                        (ssh defaultuser@192.168.2.15)
set -e
cd "$(dirname "$0")"

DEVICE=defaultuser@192.168.2.15

NAME=ru.nighteugene.aurorafitness
VERSION=1.0.0
RELEASE=1
SDK="$HOME/.local/share/aurora-sdk/sdk/5.2.1.200"
export WORKSPACE_DIR="$PWD"

mkdir -p build-docker
VAR_SPECFILE="$WORKSPACE_DIR/rpm/$NAME.spec" \
    "$SDK/tools/apptool" build --arm64 \
    --srcdir=/workspace/app --dstdir=/workspace/build-docker

RPM="$WORKSPACE_DIR/build-docker/RPMS/$NAME-$VERSION-$RELEASE.aarch64.rpm"
echo "Готово: $RPM"

if [ "$1" = "--deploy" ]; then
    echo "== deploy (пользователь defaultuser) =="
    scp "$RPM" "$DEVICE":/home/defaultuser/Downloads/
    ssh "$DEVICE" "sdk-deploy-rpm --silent /home/defaultuser/Downloads/$NAME-$VERSION-$RELEASE.aarch64.rpm"
fi
