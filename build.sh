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

# По умолчанию — USB (rndis). По Wi-Fi:
#   DEVICE=defaultuser@192.168.88.32 ./build.sh --deploy
DEVICE=${DEVICE:-defaultuser@192.168.2.15}

NAME=ru.nighteugene.aurorafitness
VERSION=1.0.0
RELEASE=1
SDK="$HOME/.local/share/aurora-sdk/sdk/5.2.1.200"
export WORKSPACE_DIR="$PWD"

ARCHES="aarch64 armv7hl x86_64"

for ARCH in $ARCHES; do
    case "$ARCH" in
        aarch64) OPT=--arm64 ;;
        armv7hl) OPT=--arm32 ;;
        x86_64)  OPT=--x64 ;;
    esac
    echo "== build $ARCH =="
    VAR_SPECFILE="$WORKSPACE_DIR/rpm/$NAME.spec" \
        "$SDK/tools/apptool" build "$OPT" \
        --srcdir=/workspace/app --dstdir=/workspace/build-docker-"$ARCH"
done

RPM="$WORKSPACE_DIR/build-docker-aarch64/RPMS/$NAME-$VERSION-$RELEASE.aarch64.rpm"
echo "Готово: build-docker-{$(echo $ARCHES | tr ' ' ',')}/RPMS/"

if [ "$1" = "--deploy" ]; then
    echo "== deploy (пользователь defaultuser, aarch64) =="
    scp "$RPM" "$DEVICE":/home/defaultuser/Downloads/
    ssh "$DEVICE" "sdk-deploy-rpm --silent /home/defaultuser/Downloads/$NAME-$VERSION-$RELEASE.aarch64.rpm"
fi
