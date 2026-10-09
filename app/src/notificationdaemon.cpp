// SPDX-License-Identifier: BSD-3-Clause

#include "notificationdaemon.h"
#include "bluezmanager.h"
#include "appsettings.h"
#include "xiaomi/xiaomichannel.h"

#include <QImage>

#include <QSocketNotifier>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

#include <dbus/dbus.h>

namespace {
// eavesdrop-матч на вызовы org.freedesktop.Notifications.Notify (сессионная шина)
const char MATCH_RULE[] =
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify',eavesdrop='true'";
const char OWN_APP_PREFIX[] = "ru.nighteugene.aurorafitness";
// Арбитраж владения браслетом: владелец этого имени на сессионной шине —
// единственный, кто работает с BLE-линком (имя отбирают CLI-режимы).
// ВАЖНО: имя запрашивается через Qt-соединение (requestBandNameQt) — на нём
// же живёт D-Bus объект BandService (/band), иначе вызовы методов по
// well-known имени уйдут в raw-соединение без объектов.
const char BAND_NAME[] = "ru.nighteugene.aurorafitness.band";
// NameAcquired/NameLost — юникаст владельцу, а имя держит Qt-соединение;
// на raw-соединении (eavesdrop) смотрим широковещательный NameOwnerChanged
const char MATCH_NAME_OWNER[] =
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameOwnerChanged',arg0='ru.nighteugene.aurorafitness.band'";
}

NotificationDaemon::NotificationDaemon(BluezManager *bluez, const QString &mac, QObject *parent)
    : QObject(parent), m_bluez(bluez), m_mac(mac)
{
    m_notificationTimer.setSingleShot(true);
    m_notificationTimer.setInterval(1000);
    connect(&m_notificationTimer, &QTimer::timeout, this, &NotificationDaemon::flushPendingNotification);
    m_minuteTimer.setInterval(60000);
    connect(&m_minuteTimer, &QTimer::timeout, this, &NotificationDaemon::onMinuteTick);

    connect(m_bluez, &BluezManager::bandReadyChanged,
            this, [this]() { if (m_bluez->bandReady()) onBandReady(); });
    connect(m_bluez, &BluezManager::deviceError,
            this, &NotificationDaemon::onDeviceError);
    connect(m_bluez, &BluezManager::bandDisconnected,
            this, &NotificationDaemon::onBandDisconnected);
}

NotificationDaemon::~NotificationDaemon()
{
    if (m_conn)
        dbus_connection_unref(m_conn);
}

bool NotificationDaemon::start()
{
    DBusError err;
    dbus_error_init(&err);
    m_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!m_conn) {
        qWarning() << "[daemon] нет сессионной шины:" << err.message;
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_set_exit_on_disconnect(m_conn, false);

    dbus_bus_add_match(m_conn, MATCH_RULE, &err);
    if (dbus_error_is_set(&err)) {
        qWarning() << "[daemon] add_match failed:" << err.message;
        dbus_error_free(&err);
        return false;
    }
    dbus_bus_add_match(m_conn, MATCH_NAME_OWNER, &err);
    if (dbus_error_is_set(&err)) {
        qWarning() << "[daemon] add_match (имена) failed:" << err.message;
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_flush(m_conn);

    // Просим имя браслета через Qt-соединение (на нём D-Bus объект /band).
    // Без DO_NOT_QUEUE: если имя занято (CLI-режим) — встаём в очередь,
    // имя вернётся само при выходе CLI (NameOwnerChanged)
    const int nameReply = requestBandNameQt();
    m_bandAllowed = (nameReply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
                     || nameReply == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER);
    qInfo() << "[daemon] имя" << BAND_NAME
            << (m_bandAllowed ? "захвачено" : "в очереди (занято CLI)");

    int fd = -1;
    if (!dbus_connection_get_unix_fd(m_conn, &fd)) {
        qWarning() << "[daemon] не удалось получить fd соединения";
        return false;
    }
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
            this, &NotificationDaemon::onDbusReadyRead);
    m_notifier->setEnabled(true);

    reloadSettings();
    m_minuteTimer.start();
    if (m_bandAllowed)
        ensureBandConnected();

    qInfo() << "[daemon] запущен. MAC:" << m_mac
            << "notifyEnabled:" << m_notifyEnabled
            << "sync:" << m_syncEnabled << "interval:" << m_syncIntervalMin << "мин";
    return true;
}

void NotificationDaemon::onDbusReadyRead()
{
    dbus_connection_read_write(m_conn, 0);
    while (DBusMessage *msg = dbus_connection_pop_message(m_conn)) {
        handleMessage(msg);
        dbus_message_unref(msg);
    }
}

void NotificationDaemon::handleMessage(DBusMessage *msg)
{
    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL) {
        handleNameSignal(msg);
        return;
    }
    if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return;
    if (!dbus_message_has_interface(msg, "org.freedesktop.Notifications")
            || !dbus_message_has_member(msg, "Notify"))
        return;
    if (!dbus_message_has_signature(msg, "susssasa{sv}i"))
        return;
    // lipstick переадресует каждый Notify на ru.auroraos.Notifications —
    // без фильтра по адресату каждое уведомление придёт дважды
    const char *dest = dbus_message_get_destination(msg);
    if (!dest || strcmp(dest, "org.freedesktop.Notifications") != 0)
        return;

    // Notify(s app_name, u replaces_id, s app_icon, s summary, s body, as actions, a{sv} hints, i timeout)
    QString appName, summary, body, hintAppId;
    DBusMessageIter it;
    if (!dbus_message_iter_init(msg, &it))
        return;
    for (int idx = 0; ; ++idx) {
        if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
            const char *s = nullptr;
            dbus_message_iter_get_basic(&it, &s);
            const QString v = QString::fromUtf8(s ? s : "");
            if (idx == 0)
                appName = v;
            else if (idx == 3)
                summary = v;
            else if (idx == 4)
                body = v;
        } else if (idx == 6
                   && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
            // hints a{sv}: ищем id приложения-источника
            DBusMessageIter arr;
            dbus_message_iter_recurse(&it, &arr);
            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter entry;
                dbus_message_iter_recurse(&arr, &entry);
                if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
                    const char *k = nullptr;
                    dbus_message_iter_get_basic(&entry, &k);
                    if (dbus_message_iter_next(&entry)
                            && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter var;
                        dbus_message_iter_recurse(&entry, &var);
                        if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                            const char *v = nullptr;
                            dbus_message_iter_get_basic(&var, &v);
                            const QString key = QString::fromUtf8(k ? k : "");
                            if ((key == QLatin1String("x-aurora-application-id")
                                 || key == QLatin1String("x-nemo-application-id")) && v)
                                hintAppId = QString::fromUtf8(v);
                        }
                    }
                }
                if (!dbus_message_iter_has_next(&arr))
                    break;
                dbus_message_iter_next(&arr);
            }
        }
        if (!dbus_message_iter_has_next(&it))
            break;
        dbus_message_iter_next(&it);
    }

    handleNotify(appName, summary, body, resolveAppPackage(appName, hintAppId));
}

void NotificationDaemon::handleNameSignal(DBusMessage *msg)
{
    if (!dbus_message_has_interface(msg, "org.freedesktop.DBus")
            || !dbus_message_has_member(msg, "NameOwnerChanged"))
        return;

    const char *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
    if (!dbus_message_get_args(msg, nullptr,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &oldOwner,
                               DBUS_TYPE_STRING, &newOwner,
                               DBUS_TYPE_INVALID)
            || !name || strcmp(name, BAND_NAME) != 0)
        return;

    const QString mine = QDBusConnection::sessionBus().baseService();
    const QString newO = QString::fromUtf8(newOwner ? newOwner : "");
    if (!newO.isEmpty() && newO == mine) {
        if (m_bandAllowed)
            return;
        qInfo() << "[daemon] имя браслета получено";
        m_bandAllowed = true;
        reloadSettings();
        ensureBandConnected();
        if (m_syncPending)
            requestSync();
        flushPendingNotification();
        return;
    }

    if (m_bandAllowed) {
        qInfo() << "[daemon] имя браслета потеряно (CLI активен) — отключаюсь";
        m_bandAllowed = false;
        m_connecting = false;
        m_bluez->disconnectBand();
    }
    // Встаём в очередь за именем: получим его обратно, когда CLI завершится
    requestBandNameQt();
}

int NotificationDaemon::requestBandNameQt()
{
    QDBusInterface dbusIface(QStringLiteral("org.freedesktop.DBus"),
                             QStringLiteral("/org/freedesktop/DBus"),
                             QStringLiteral("org.freedesktop.DBus"),
                             QDBusConnection::sessionBus());
    QDBusReply<uint> reply = dbusIface.call(QStringLiteral("RequestName"),
                                            QString::fromLatin1(BAND_NAME),
                                            uint(DBUS_NAME_FLAG_ALLOW_REPLACEMENT));
    if (!reply.isValid()) {
        qWarning() << "[daemon] RequestName:" << reply.error().message();
        return -1;
    }
    return int(reply.value());
}

void NotificationDaemon::handleNotify(const QString &appName,
                                      const QString &summary, const QString &body,
                                      const QString &package)
{
    if (appName.startsWith(QLatin1String(OWN_APP_PREFIX)))
        return; // свои уведомления не пересылаем
    if (summary.isEmpty() && body.isEmpty())
        return;

    reloadSettings();
    if (!m_notifyEnabled || m_mac.isEmpty()) return;
    cacheIcon(package);
    // Ограниченная очередь: до 20 сообщений, не старше пяти минут.
    if (m_notifications.size() >= 20) m_notifications.dequeue();
    m_notifications.enqueue({appName, summary, body, package, m_mac,
                             QDateTime::currentDateTime()});
    if (!m_notificationTimer.isActive()) flushPendingNotification();
}

void NotificationDaemon::flushPendingNotification()
{
    reloadSettings();
    if (!m_notifyEnabled || !m_bandAllowed) return;
    const QDateTime now = QDateTime::currentDateTime();
    while (!m_notifications.isEmpty()) {
        const auto &pending = m_notifications.head();
        const qint64 age = pending.created.secsTo(now);
        if (pending.address == m_mac && age >= 0 && age <= 300) break;
        m_notifications.dequeue();
    }
    if (m_notifications.isEmpty()) return;
    if (!m_bluez->bandReady()) {
        ensureBandConnected();
        return;
    }
    const PendingNotification pending = m_notifications.dequeue();
    m_bluez->sendTestNotification(pending.title, pending.body, pending.app, pending.package);
    if (!m_notifications.isEmpty()) m_notificationTimer.start();
}

void NotificationDaemon::ensureBandConnected()
{
    if (!m_bandAllowed)
        return; // браслетом владеет CLI — pending-флаги уже выставлены
    if (m_connecting || m_bluez->bandReady())
        return;
    if (m_mac.isEmpty()) {
        qWarning() << "[daemon] MAC браслета не задан (аргумент или miband8/lastAddress)";
        return;
    }
    if (!m_bluez->adapterPowered()) {
        qInfo() << "[daemon] Bluetooth выключен — пробую включить";
        if (!m_bluez->powerOnAdapter()) {
            qWarning() << "[daemon] включить BT не удалось, повтор при следующем уведомлении";
            return;
        }
    }
    qInfo() << "[daemon] подключение к" << m_mac;
    m_connecting = true;
    m_bluez->connectToBand(m_mac);
}

void NotificationDaemon::onBandReady()
{
    m_connecting = false;
    reloadSettings();
    if (!m_bandAllowed || !m_bluez->bandReady()) return;
    if (m_syncPending && m_syncEnabled) {
        m_syncPending = false;
        qInfo() << "[daemon] автосинк";
        m_bluez->syncActivity();
    }
    flushPendingNotification();
}

void NotificationDaemon::onDeviceError(const QString &message)
{
    qWarning() << "[daemon] ошибка устройства:" << message;
    m_connecting = false;
}

void NotificationDaemon::onBandDisconnected()
{
    qInfo() << "[daemon] соединение разорвано, переподключение при следующем событии";
    m_connecting = false;
}

void NotificationDaemon::requestSync()
{
    if (!m_syncEnabled) return;
    if (!m_bandAllowed) {
        m_syncPending = true; // синк уйдёт, когда CLI отпустит браслет
        return;
    }
    if (m_bluez->bandReady()) {
        qInfo() << "[daemon] автосинк";
        m_bluez->syncActivity();
        return;
    }
    m_syncPending = true;
    ensureBandConnected();
}

void NotificationDaemon::onMinuteTick()
{
    reloadSettings();
    if (m_bandAllowed)
        ensureBandConnected();
    if (!m_bandAllowed || !m_syncEnabled || m_syncIntervalMin <= 0)
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime previous = m_lastAttempt > m_lastSync ? m_lastAttempt : m_lastSync;
    if (previous.isValid() && previous.secsTo(now) >= 0
            && previous.secsTo(now) < qint64(m_syncIntervalMin) * 60) return;
    // Отмечаем попытку сразу, чтобы при обрыве связи не долбить браслет каждую минуту
    m_lastAttempt = now;
    requestSync();
}

void NotificationDaemon::reloadSettings()
{
    // Перечитываем conf-файл вручную: QSettings кэширует и не гарантирует
    // подхват внешних изменений (их делает GUI-процесс).
    const QMap<QString, QString> conf = readConfFile();
    m_notifyEnabled = conf.value(QStringLiteral("daemon/notifyEnabled")) == QLatin1String("true");
    m_syncEnabled = conf.value(QStringLiteral("daemon/syncEnabled")) == QLatin1String("true");
    const QString address = conf.value(QStringLiteral("device/lastAddress"), conf.value(QStringLiteral("miband8/lastAddress")));
    if (!m_notifyEnabled) {
        m_notifications.clear();
        m_notificationTimer.stop();
    }
    if (!m_syncEnabled) m_syncPending = false;
    if (!address.isEmpty() && address != m_mac) {
        m_notifications.clear();
        m_syncPending = false;
        m_lastAttempt = QDateTime();
        m_mac = address;
        m_connecting = false;
        if (m_bandAllowed && !m_bluez->connectedAddress().isEmpty()
                && m_bluez->connectedAddress().compare(address, Qt::CaseInsensitive) != 0)
            m_bluez->disconnectBand();
    }
    bool ok = false;
    const int interval = conf.value(QStringLiteral("daemon/syncIntervalMin"),
                                    QStringLiteral("30")).toInt(&ok);
    m_syncIntervalMin = ok ? qBound(1, interval, 1440) : 30;
    const QDateTime lastSync = QDateTime::fromString(
                conf.value(lastSyncSettingsKey(m_mac)), Qt::ISODate);
    m_lastSync = lastSync;
}

QMap<QString, QString> NotificationDaemon::readConfFile() const
{
    QMap<QString, QString> out;
    QFile f(appSettingsPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&f);
    in.setCodec("UTF-8");
    QString group;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        out.insert(group + QLatin1Char('/') + line.left(eq).trimmed(),
                   line.mid(eq + 1).trimmed());
    }
    return out;
}

void NotificationDaemon::cacheIcon(const QString &package) const
{
    if (!safeIconPackage(package))
        return;
    // package — desktop-id или маркер (__system/__unknown): безопасное имя файла
    const QString dir = appConfigDir() + QStringLiteral("/icons");
    QDir().mkpath(dir);
    const QString target = dir + QStringLiteral("/") + package + QStringLiteral(".png");
    if (QFile::exists(target))
        return;
    const QStringList paths = XiaomiChannel::iconCandidatePaths(package);
    QImage img;
    for (const QString &p : paths) {
        if (img.load(p))
            break;
    }
    if (!img.isNull())
        img.save(target, "PNG");
}

QString NotificationDaemon::resolveAppPackage(const QString &appName,
                                              const QString &hintId) const
{
    if (safeIconPackage(hintId))
        return hintId;

    const QString lower = appName.toLower();

    // Отправители без собственного desktop-файла: системные уведомления
    // (зарядка, режим разработчика, снимки экрана) — шестерёнка; агрегированные
    // оповещения календаря шлёт демон календаря — мапим на приложение календаря
    static const QMap<QString, QString> aliases = {
        {QStringLiteral("система"), QStringLiteral("__system")},
        {QStringLiteral("пропущенные оповещения календаря"),
         QStringLiteral("ru.omp.calendar")},
    };
    const QString alias = aliases.value(lower);
    if (!alias.isEmpty())
        return alias;

    // appName в Notify — локализованное имя приложения; ищем его в
    // desktop-файлах, id = имя файла без .desktop (по нему лежит иконка)
    static QMap<QString, QString> byName; // lower(name) -> desktop id
    if (byName.isEmpty()) {
        const QStringList files = QDir(QStringLiteral("/usr/share/applications"))
                .entryList(QStringList() << QStringLiteral("*.desktop"), QDir::Files);
        for (const QString &f : files) {
            QFile file(QStringLiteral("/usr/share/applications/") + f);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QString id = f.left(f.size() - 8); // без ".desktop"
            QTextStream in(&file);
            in.setCodec("UTF-8");
            while (!in.atEnd()) {
                const QString line = in.readLine();
                if (!line.startsWith(QLatin1String("Name")))
                    continue;
                const int eq = line.indexOf(QLatin1Char('='));
                if (eq > 0)
                    byName.insert(line.mid(eq + 1).trimmed().toLower(), id);
            }
        }
    }
    const QString hit = byName.value(lower);
    if (!hit.isEmpty())
        return hit;

    // appName сам может оказаться desktop-id (нелокализованным)
    if (QFile::exists(QStringLiteral("/usr/share/applications/") + appName
                      + QStringLiteral(".desktop")))
        return appName;

    return QStringLiteral("__unknown");
}
