#include "wearablechannel.h"
#include "pinetimechannel.h"
#include "xiaomi/xiaomichannel.h"

WearableChannel *createWearableChannel(const QVariantList &services, QObject *parent)
{
    QMap<QString, QString> paths;
    QStringList uuids;
    for (const QVariant &service : services) {
        const QVariantMap s = service.toMap();
        uuids.append(s.value(QStringLiteral("uuid")).toString().toLower());
        for (const QVariant &characteristic : s.value(QStringLiteral("characteristics")).toList()) {
            const QVariantMap c = characteristic.toMap();
            paths.insert(c.value(QStringLiteral("uuid")).toString().toLower(), c.value(QStringLiteral("path")).toString());
        }
    }
    if (uuids.contains(QStringLiteral("00030000-78fc-48fe-8e23-433b3a1942d0"))
            || uuids.contains(QStringLiteral("00000000-78fc-48fe-8e23-433b3a1942d0"))) {
        auto channel = new PineTimeChannel(parent);
        channel->setup(services);
        return channel;
    }
    const QString suffix = QStringLiteral("-0000-1000-8000-00805f9b34fb");
    if (uuids.contains(QStringLiteral("0000fe95") + suffix)
            && paths.contains(QStringLiteral("00000051") + suffix)
            && paths.contains(QStringLiteral("00000052") + suffix)) {
        auto channel = new XiaomiChannel(parent);
        channel->setup(paths.value(QStringLiteral("00000051") + suffix),
                       paths.value(QStringLiteral("00000052") + suffix),
                       paths.value(QStringLiteral("00000053") + suffix),
                       paths.value(QStringLiteral("00000055") + suffix));
        return channel;
    }
    return nullptr;
}
