// SPDX-License-Identifier: BSD-3-Clause

#ifndef XIAOMI_ACTIVITYPARSER_H
#define XIAOMI_ACTIVITYPARSER_H
#include <QByteArray>
#include <QVariantMap>
#include <QVariantList>

namespace xiaomiactivity {

// FileId — 7 байт: [0..3] timestamp u32le, [4] timezone i8 (15-мин блоки),
// [5] version u8, [6] flags: bit7 type (0=ACTIVITY,1=SPORTS),
// bits2..6 subtype, bits0..1 detailType (0=DETAILS,1=SUMMARY,2=GPS)
struct FileId {
    quint32 timestamp = 0;
    qint8 timezone = 0;
    quint8 version = 0;
    quint8 type = 0;      // 0=ACTIVITY, 1=SPORTS
    quint8 subtype = 0;   // ACTIVITY: 0=daily, 3=sleep stages, 6=manual samples, 8=sleep details
    quint8 detailType = 0;
    bool valid = false;
};

FileId parseFileId(const QByteArray &id7);

// payload — тело файла БЕЗ fileId(7), padding(1) и CRC32(4) в конце.
// Возвращает QVariantMap с ключом "kind" и данными (ниже).
// Неподдерживаемый формат/версия: {"kind":"unknown", "reason": "..."}.
QVariantMap parseActivityFile(const FileId &id, const QByteArray &payload);

} // namespace xiaomiactivity
#endif
