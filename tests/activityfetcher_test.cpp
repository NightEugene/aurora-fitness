// SPDX-License-Identifier: BSD-3-Clause
#include "app/src/xiaomi/activityfetcher.h"
#include "app/src/xiaomi/proto.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <cassert>
#include <cstdio>

static QByteArray frame(const QByteArray &id)
{
    QByteArray file = id + QByteArray(1 + 57, 0); // summary v5 без валидных показателей
    quint32 crc = 0xffffffffu;
    for (char byte : file) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    crc ^= 0xffffffffu;
    for (int i = 0; i < 4; ++i) file.append(char(crc >> (8 * i)));
    return QByteArray::fromHex("01000100") + file;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray id = QByteArray::fromHex("00123456000501");
    pb::Writer list;
    list.bytes(2, id);
    for (int scenario = 0; scenario < 4; ++scenario) {
        QList<quint32> commands;
        int saves = 0;
        bool finished = false, success = true;
        ActivityFetcher fetcher(nullptr,
            [&](quint32 type, const QByteArray &) { commands << type; },
            [&](const QByteArray &, const QVariantMap &) { ++saves; return scenario != 1; });
        QObject::connect(&fetcher, &ActivityFetcher::finished, [&](bool ok) { finished = true; success = ok; });
        fetcher.start();
        fetcher.start(); // повторный запрос не должен сбрасывать текущую загрузку
        assert(commands.size() == 1);
        fetcher.handleHealthResponse(1, list.data);
        fetcher.handleHealthResponse(2, QByteArray());
        QByteArray data = frame(id);
        if (scenario == 2) data[data.size() - 1] = char(data.at(data.size() - 1) ^ 1); // неправильная CRC
        if (scenario == 3) {
            QEventLoop loop;
            QObject::connect(&fetcher, &ActivityFetcher::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(6500, &loop, &QEventLoop::quit);
            loop.exec(); // таймаут файла, без аппаратного BLE
        } else fetcher.addFilePortion(data);
        assert(finished && success == (scenario == 0));
        assert(commands.contains(5) == (scenario == 0));
        assert(saves == (scenario <= 1 ? 1 : 0));
    }
    std::puts("ActivityFetcher: сохранение перед ACK, отказ хранилища, CRC, таймаут и повторный старт — успешно");
}
