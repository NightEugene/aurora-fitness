#include "mprisbridge.h"

#include <QtDBus>
#include <algorithm>

namespace {
const QString player = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString relay = QStringLiteral("ru.nighteugene.aurorafitness.media");
QVariant unbox(const QVariant &value)
{
    return value.userType() == qMetaTypeId<QDBusVariant>()
            ? value.value<QDBusVariant>().variant() : value;
}
}

MprisBridge::MprisBridge(QObject *parent, bool useRelay) : QObject(parent), m_useRelay(useRelay)
{
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &MprisBridge::refresh);
}

void MprisBridge::start()
{
    m_timer.start();
    refresh();
}

void MprisBridge::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    if (!m_useRelay) {
        refreshPlayers();
        return;
    }
    QDBusMessage request = QDBusMessage::createMethodCall(relay, QStringLiteral("/media"), relay,
            QStringLiteral("getProperties"));
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            m_usingRelay = false;
            refreshPlayers();
            return;
        }
        m_usingRelay = true;
        m_properties = reply.value();
        m_refreshing = false;
        emit propertiesChanged(m_properties);
    });
}

void MprisBridge::refreshPlayers()
{
    const QDBusMessage request = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        QDBusPendingReply<QStringList> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            m_refreshing = false;
            return;
        }
        QStringList services;
        for (const QString &name : reply.value()) {
            if (name.startsWith(QStringLiteral("org.mpris.MediaPlayer2.")))
                services.append(name);
        }
        if (services.isEmpty()) {
            m_service.clear();
            m_properties.clear();
            m_refreshing = false;
            emit propertiesChanged(m_properties);
            return;
        }
        auto remaining = QSharedPointer<int>::create(services.size());
        auto candidates = QSharedPointer<QMap<QString, QVariantMap>>::create();
        for (const QString &service : services) {
            QDBusMessage get = QDBusMessage::createMethodCall(service, path,
                    QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
            get << player;
            auto pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(get), this);
            connect(pending, &QDBusPendingCallWatcher::finished, this,
                    [this, pending, service, remaining, candidates]() {
                QDBusPendingReply<QVariantMap> properties = *pending;
                pending->deleteLater();
                if (!properties.isError())
                    candidates->insert(service, properties.value());
                if (--*remaining != 0)
                    return;
                QString selected = candidates->contains(m_service) ? m_service : QString();
                for (auto it = candidates->constBegin(); it != candidates->constEnd(); ++it) {
                    if (selected.isEmpty())
                        selected = it.key();
                    if (unbox(it.value().value(QStringLiteral("PlaybackStatus"))).toString()
                            == QStringLiteral("Playing")) {
                        selected = it.key();
                        if (selected == m_service)
                            break;
                    }
                }
                m_service = selected;
                m_properties = candidates->value(selected);
                m_refreshing = false;
                emit propertiesChanged(m_properties);
            });
        }
    });
}

void MprisBridge::command(quint8 event)
{
    if (m_usingRelay) {
        QDBusMessage request = QDBusMessage::createMethodCall(relay, QStringLiteral("/media"), relay,
                QStringLiteral("control"));
        request << int(event);
        QDBusConnection::sessionBus().asyncCall(request, 3000);
        return;
    }
    if (event == 0xe0) {
        refresh();
        return;
    }
    if (m_service.isEmpty() || !unbox(m_properties.value(QStringLiteral("CanControl"))).toBool())
        return;
    const QMap<int, QString> methods = {{0, QStringLiteral("Play")}, {1, QStringLiteral("Pause")},
                                      {3, QStringLiteral("Next")}, {4, QStringLiteral("Previous")}};
    const QMap<int, QString> capabilities = {{0, QStringLiteral("CanPlay")}, {1, QStringLiteral("CanPause")},
                                            {3, QStringLiteral("CanGoNext")}, {4, QStringLiteral("CanGoPrevious")}};
    QDBusMessage request;
    if (methods.contains(event)) {
        if (!unbox(m_properties.value(capabilities.value(event))).toBool())
            return;
        request = QDBusMessage::createMethodCall(m_service, path, player, methods.value(event));
    } else if (event == 5 || event == 6) {
        if (!m_properties.contains(QStringLiteral("Volume")))
            return;
        const double volume = std::max(0.0, std::min(1.0,
                unbox(m_properties.value(QStringLiteral("Volume"))).toDouble() + (event == 5 ? 0.05 : -0.05)));
        m_properties.insert(QStringLiteral("Volume"), volume);
        request = QDBusMessage::createMethodCall(m_service, path,
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
        request << player << QStringLiteral("Volume") << QVariant::fromValue(QDBusVariant(volume));
    } else {
        return;
    }
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError())
            qWarning() << "MPRIS:" << reply.error().message();
        watcher->deleteLater();
        refresh();
    });
}

MprisRelay::MprisRelay(QObject *parent) : QObject(parent), m_bridge(this, false)
{
}

bool MprisRelay::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage request = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
            QStringLiteral("/org/freedesktop/DBus"), QStringLiteral("org.freedesktop.DBus"), QStringLiteral("RequestName"));
    request << relay << uint(4);
    QDBusReply<uint> reply = bus.call(request);
    if (!reply.isValid() || (reply.value() != 1 && reply.value() != 4))
        return false;
    if (!bus.registerObject(QStringLiteral("/media"), this, QDBusConnection::ExportAllSlots))
        return false;
    m_bridge.start();
    return true;
}

QVariantMap MprisRelay::getProperties() const
{
    return m_bridge.properties();
}

void MprisRelay::control(int event)
{
    if (event >= 0 && event <= 255)
        m_bridge.command(quint8(event));
}
