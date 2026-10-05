#include "pinetimechannel.h"
#include "mprisbridge.h"

#include <QtDBus>
#include <QDateTime>
#include <QtEndian>

namespace {
const QString suffix = QStringLiteral("-78fc-48fe-8e23-433b3a1942d0");
const QString steps = QStringLiteral("00030001") + suffix;
const QString events = QStringLiteral("00000001") + suffix;
const QString battery = QStringLiteral("00002a19-0000-1000-8000-00805f9b34fb");
const QString heartRate = QStringLiteral("00002a37-0000-1000-8000-00805f9b34fb");
const QString currentTime = QStringLiteral("00002a2b-0000-1000-8000-00805f9b34fb");
const QString alert = QStringLiteral("00002a46-0000-1000-8000-00805f9b34fb");

QByteArray utf8(const QString &text, int limit)
{
    QByteArray result;
    const QVector<uint> points = text.toUcs4();
    for (uint point : points) {
        if (point == 0)
            point = ' ';
        const QByteArray encoded = QString::fromUcs4(&point, 1).toUtf8();
        if (result.size() + encoded.size() > limit)
            break;
        result.append(encoded);
    }
    return result;
}

QByteArray big32(quint32 value)
{
    QByteArray bytes(4, 0);
    qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QVariantMap variantMap(QVariant value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    if (value.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}
}

PineTimeChannel::PineTimeChannel(QObject *parent) : WearableChannel(parent), m_mpris(new MprisBridge(this))
{
    m_poll.setInterval(60000);
    connect(&m_poll, &QTimer::timeout, this, &PineTimeChannel::sync);
    connect(m_mpris, &MprisBridge::propertiesChanged, this, &PineTimeChannel::publishMusic);
}

void PineTimeChannel::setup(const QVariantList &services)
{
    for (const QVariant &service : services) {
        for (const QVariant &characteristic : service.toMap().value(QStringLiteral("characteristics")).toList()) {
            const QVariantMap c = characteristic.toMap();
            const QString uuid = c.value(QStringLiteral("uuid")).toString().toLower();
            m_paths.insert(uuid, c.value(QStringLiteral("path")).toString());
            m_flags.insert(uuid, c.value(QStringLiteral("flags")).toStringList());
        }
    }
}

void PineTimeChannel::start()
{
    m_starting = true;
    for (const QString &uuid : {steps, heartRate, battery, events}) {
        if (m_flags.value(uuid).contains(QStringLiteral("notify")))
            enqueue(uuid, QStringLiteral("StartNotify"));
    }
    setTime();
    for (const QString &uuid : {steps, heartRate, battery}) {
        if (m_flags.value(uuid).contains(QStringLiteral("read")))
            enqueue(uuid, QStringLiteral("ReadValue"));
    }
    pump();
}

void PineTimeChannel::enqueue(const QString &uuid, const QString &method, const QByteArray &data)
{
    if (!m_paths.contains(uuid))
        return;
    m_queue.enqueue({uuid, method, data, 0});
    if (m_queue.size() == 1)
        QTimer::singleShot(0, this, &PineTimeChannel::pump);
}

void PineTimeChannel::pump()
{
    if (m_running)
        return;
    if (m_queue.isEmpty()) {
        if (m_starting) {
            m_starting = false;
            m_ready = !m_syncFailed;
            if (m_ready) {
                m_poll.start();
                m_mpris->start();
                emit readyChanged();
            }
            return;
        }
        if (m_syncing) {
            m_syncing = false;
            if (!m_syncFailed)
                emit activityFetchFinished();
        }
        return;
    }
    m_running = true;
    const Operation op = m_queue.head();
    QDBusMessage request = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"),
            m_paths.value(op.uuid), QStringLiteral("org.bluez.GattCharacteristic1"), op.method);
    if (op.method == QStringLiteral("ReadValue")) {
        request << QVariantMap();
    } else if (op.method == QStringLiteral("WriteValue")) {
        QVariantMap options;
        options.insert(QStringLiteral("type"), QStringLiteral("request"));
        request << op.data << options;
    }
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(request, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, op]() {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        m_running = false;
        if (reply.type() == QDBusMessage::ErrorMessage
                && (reply.errorName().endsWith(QStringLiteral("InProgress"))
                    || reply.errorMessage().contains(QStringLiteral("In Progress")))
                && m_queue.head().retries++ < 10) {
            m_running = true;
            QTimer::singleShot(100, this, [this]() { m_running = false; pump(); });
            return;
        }
        m_queue.dequeue();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "PineTime" << op.method << op.uuid << reply.errorMessage();
            if (op.method == QStringLiteral("WriteValue"))
                m_musicValues.remove(op.uuid);
            if (m_starting || m_syncing)
                m_syncFailed = true;
            emit error(QStringLiteral("PineTime: %1 (%2)").arg(reply.errorMessage(), op.method));
        } else if (op.method == QStringLiteral("ReadValue") && !reply.arguments().isEmpty()) {
            onCharacteristicValue(m_paths.value(op.uuid), reply.arguments().first().toByteArray());
        }
        pump();
    });
}

void PineTimeChannel::setTime()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate date = now.date();
    const QTime time = now.time();
    QByteArray value(10, 0);
    qToLittleEndian(quint16(date.year()), reinterpret_cast<uchar *>(value.data()));
    value[2] = char(date.month());
    value[3] = char(date.day());
    value[4] = char(time.hour());
    value[5] = char(time.minute());
    value[6] = char(time.second());
    value[7] = char(date.dayOfWeek());
    value[8] = char(time.msec() * 256 / 1000);
    value[9] = 1;
    enqueue(currentTime, QStringLiteral("WriteValue"), value);
}

void PineTimeChannel::sync()
{
    if (!m_ready || m_syncing)
        return;
    m_syncing = true;
    m_syncFailed = false;
    setTime();
    for (const QString &uuid : {steps, heartRate, battery}) {
        if (m_flags.value(uuid).contains(QStringLiteral("read")))
            enqueue(uuid, QStringLiteral("ReadValue"));
    }
    pump();
}

void PineTimeChannel::sendNotification(const QString &app, const QString &title, const QString &body,
                                       const QString &package)
{
    Q_UNUSED(package)
    if (!m_paths.contains(alert)) {
        emit error(QStringLiteral("PineTime: сервис уведомлений недоступен"));
        return;
    }
    QString message = title.isEmpty() ? app : title;
    if (!body.isEmpty()) {
        if (!message.isEmpty())
            message += QLatin1Char('\n');
        message += body;
    }
    if (message.isEmpty())
        return;
    QByteArray packet = QByteArray::fromHex("000100");
    packet += utf8(message, 99);
    enqueue(alert, QStringLiteral("WriteValue"), packet);
}

void PineTimeChannel::onCharacteristicValue(const QString &path, const QByteArray &value)
{
    const QString uuid = m_paths.key(path);
    const auto bytes = reinterpret_cast<const uchar *>(value.constData());
    if (uuid == steps && value.size() == 4) {
        emit stepsReceived(qFromLittleEndian<quint32>(bytes));
    } else if (uuid == battery && value.size() == 1 && bytes[0] <= 100) {
        emit batteryReceived(bytes[0], 0);
    } else if (uuid == heartRate && value.size() >= 2) {
        if ((bytes[0] & 1) && value.size() < 3)
            return;
        const int bpm = (bytes[0] & 1) ? qFromLittleEndian<quint16>(bytes + 1) : bytes[1];
        if (bpm > 0)
            emit heartRateReceived(bpm);
    } else if (uuid == events && value.size() == 1) {
        if (m_eventTime.isValid() && m_lastEvent == value && m_eventTime.elapsed() < 100)
            return;
        m_lastEvent = value;
        m_eventTime.restart();
        m_mpris->command(bytes[0]);
    }
}

void PineTimeChannel::publishMusic(const QVariantMap &properties)
{
    if (!m_ready || m_running || !m_queue.isEmpty())
        return;
    const QVariantMap metadata = variantMap(properties.value(QStringLiteral("Metadata")));
    const bool playing = properties.value(QStringLiteral("PlaybackStatus")).toString() == QStringLiteral("Playing");
    const QMap<QString, QByteArray> values = {
        {QStringLiteral("00000002") + suffix, QByteArray(1, playing ? 1 : 0)},
        {QStringLiteral("00000003") + suffix, utf8(metadata.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", ")), 40)},
        {QStringLiteral("00000004") + suffix, utf8(metadata.value(QStringLiteral("xesam:title")).toString(), 40)},
        {QStringLiteral("00000005") + suffix, utf8(metadata.value(QStringLiteral("xesam:album")).toString(), 40)},
        {QStringLiteral("00000006") + suffix, big32(quint32(properties.value(QStringLiteral("Position")).toLongLong() / 1000000))},
        {QStringLiteral("00000007") + suffix, big32(quint32(metadata.value(QStringLiteral("mpris:length")).toLongLong() / 1000000))},
        {QStringLiteral("0000000a") + suffix, big32(quint32(properties.value(QStringLiteral("Rate"), 1.0).toDouble() * 100))}
    };
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!m_musicValues.contains(it.key()) || m_musicValues.value(it.key()) != it.value())
            enqueue(it.key(), QStringLiteral("WriteValue"), it.value());
    }
    m_musicValues = values;
}
