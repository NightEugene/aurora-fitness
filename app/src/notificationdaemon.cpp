// SPDX-License-Identifier: BSD-3-Clause

#include "notificationdaemon.h"
#include "bluezmanager.h"
#include "appsettings.h"

#include <QSocketNotifier>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusInterface>

#include <dbus/dbus.h>

namespace {
// eavesdrop-матч на вызовы org.freedesktop.Notifications.Notify (сессионная шина)
const char MATCH_RULE[] =
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify',eavesdrop='true'";
const char OWN_APP_PREFIX[] = "ru.nighteugene.aurorafitness";
// Relay-имя GUI: демон шлёт сюда уведомления, когда браслетом владеет GUI
const char RELAY_NAME[] = "ru.nighteugene.aurorafitness.gui";
// Арбитраж владения браслетом: владелец этого имени на сессионной шине —
// единственный, кто работает с BLE-линком (GUI отбирает у демона)
const char BAND_NAME[] = "ru.nighteugene.aurorafitness.band";
const char MATCH_NAME_ACQUIRED[] =
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameAcquired',arg0='ru.nighteugene.aurorafitness.band'";
const char MATCH_NAME_LOST[] =
        "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',"
        "member='NameLost',arg0='ru.nighteugene.aurorafitness.band'";
}

NotificationDaemon::NotificationDaemon(BluezManager *bluez, const QString &mac, QObject *parent)
    : QObject(parent), m_bluez(bluez), m_mac(mac)
{
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
    if (m_relayMode) {
        // Ретранслятор внутри GUI: без eavesdrop и без имени браслета —
        // принимает forwardNotification от демона и шлёт на браслет,
        // которым владеет GUI. registerService() на Qt 5.6 сломан — имя
        // просим напрямую через RequestName (флаги 0).
        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusInterface dbusIface(QStringLiteral("org.freedesktop.DBus"),
                                 QStringLiteral("/org/freedesktop/DBus"),
                                 QStringLiteral("org.freedesktop.DBus"), bus);
        dbusIface.call(QStringLiteral("RequestName"),
                       QString::fromLatin1(RELAY_NAME), 0u);
        bus.registerObject(QStringLiteral("/notify"), this,
                           QDBusConnection::ExportAllSlots);
        m_bandAllowed = true; // браслетом владеет сам GUI
        reloadSettings();
        qInfo() << "[relay] пересылка уведомлений через GUI активна, MAC:" << m_mac;
        return true;
    }

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
    dbus_bus_add_match(m_conn, MATCH_NAME_ACQUIRED, &err);
    dbus_bus_add_match(m_conn, MATCH_NAME_LOST, &err);
    if (dbus_error_is_set(&err)) {
        qWarning() << "[daemon] add_match (имена) failed:" << err.message;
        dbus_error_free(&err);
        return false;
    }
    dbus_connection_flush(m_conn);

    // Просим имя браслета. Без DO_NOT_QUEUE: если имя у GUI — встаём в очередь,
    // имя вернётся само при закрытии GUI (NameAcquired)
    const int nameReply = dbus_bus_request_name(m_conn, BAND_NAME,
                                                DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &err);
    if (dbus_error_is_set(&err)) {
        qWarning() << "[daemon] request_name failed:" << err.message;
        dbus_error_free(&err);
    } else {
        m_bandAllowed = (nameReply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER
                         || nameReply == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER);
        qInfo() << "[daemon] имя" << BAND_NAME
                << (m_bandAllowed ? "захвачено" : "в очереди (GUI активен)");
    }
    dbus_connection_flush(m_conn);

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
    if (!dbus_message_has_interface(msg, "org.freedesktop.DBus"))
        return;
    const bool acquired = dbus_message_has_member(msg, "NameAcquired");
    const bool lost = dbus_message_has_member(msg, "NameLost");
    if (!acquired && !lost)
        return;

    const char *name = nullptr;
    if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID)
            || !name || strcmp(name, BAND_NAME) != 0)
        return;

    if (lost) {
        qInfo() << "[daemon] имя браслета потеряно (GUI активен) — отключаюсь";
        m_bandAllowed = false;
        m_connecting = false;
        m_bluez->disconnectBand();
        // Встаём в очередь за именем: получим NameAcquired, когда GUI
        // закроется или свернётся и отпустит имя
        DBusError err;
        dbus_error_init(&err);
        dbus_bus_request_name(m_conn, BAND_NAME, DBUS_NAME_FLAG_ALLOW_REPLACEMENT, &err);
        if (dbus_error_is_set(&err)) {
            qWarning() << "[daemon] request_name (в очередь) failed:" << err.message;
            dbus_error_free(&err);
        }
        dbus_connection_flush(m_conn);
        return;
    }

    qInfo() << "[daemon] имя браслета получено";
    m_bandAllowed = true;
    reloadSettings();
    ensureBandConnected();
    if (m_syncPending)
        requestSync();
    flushPendingNotification();
}

void NotificationDaemon::handleNotify(const QString &appName,
                                      const QString &summary, const QString &body,
                                      const QString &package)
{
    if (appName.startsWith(QLatin1String(OWN_APP_PREFIX)))
        return; // свои уведомления не пересылаем
    if (summary.isEmpty() && body.isEmpty())
        return;

    qInfo() << "[daemon] Notify от" << appName << "—" << summary << "/" << body
            << "pkg:" << package;

    if (!m_notifyEnabled)
        return;

    if (!m_bandAllowed) {
        // Браслетом владеет GUI — передаём уведомление ему через шину.
        // GUI в песочнице (xdg-dbus-proxy), eavesdrop там недоступен.
        QDBusInterface gui(QString::fromLatin1(RELAY_NAME),
                           QStringLiteral("/notify"),
                           QString::fromLatin1(RELAY_NAME),
                           QDBusConnection::sessionBus());
        gui.call(QDBus::NoBlock, QStringLiteral("forwardNotification"),
                 appName, summary, body, package);
        return;
    }

    // Запоминаем последнее уведомление — уйдёт на браслет, когда тот будет готов
    m_pendingApp = appName;
    m_pendingTitle = summary;
    m_pendingBody = body;
    m_pendingPackage = package;
    m_pendingNotification = true;
    flushPendingNotification();
}

void NotificationDaemon::flushPendingNotification()
{
    if (!m_pendingNotification)
        return;
    if (!m_bluez->bandReady()) {
        ensureBandConnected();
        return;
    }
    m_pendingNotification = false;
    qInfo() << "[daemon] отправка на браслет:" << m_pendingTitle;
    m_bluez->sendTestNotification(m_pendingTitle, m_pendingBody, m_pendingApp,
                                  m_pendingPackage);
}

void NotificationDaemon::ensureBandConnected()
{
    if (!m_bandAllowed)
        return; // браслетом владеет GUI — pending-флаги уже выставлены
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
    if (m_syncPending) {
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
    if (!m_bandAllowed) {
        m_syncPending = true; // синк уйдёт, когда GUI отпустит браслет
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
    if (m_lastSync.isValid() && m_lastSync.secsTo(now) < m_syncIntervalMin * 60)
        return;
    // Отмечаем попытку сразу, чтобы при обрыве связи не долбить браслет каждую минуту
    m_lastSync = now;
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
    if (!address.isEmpty() && address != m_mac) {
        m_mac = address;
        m_connecting = false;
        if (m_bandAllowed)
            m_bluez->disconnectBand();
    }
    bool ok = false;
    const int interval = conf.value(QStringLiteral("daemon/syncIntervalMin"),
                                    QStringLiteral("30")).toInt(&ok);
    m_syncIntervalMin = ok ? interval : 30;
    const QDateTime lastSync = QDateTime::fromString(
                conf.value(QStringLiteral("device/lastSyncTime"), conf.value(QStringLiteral("miband8/lastSyncTime"))), Qt::ISODate);
    if (lastSync.isValid())
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

void NotificationDaemon::forwardNotification(const QString &appName,
                                             const QString &title, const QString &body,
                                             const QString &package)
{
    // Relay-режим (GUI): демон передал уведомление — шлём на браслет,
    // которым владеет GUI. Настройки перечитываем на каждый вызов.
    reloadSettings();
    if (!m_notifyEnabled)
        return;
    if (appName.startsWith(QLatin1String(OWN_APP_PREFIX)))
        return;
    if (title.isEmpty() && body.isEmpty())
        return;
    qInfo() << "[relay] пересылка на браслет:" << appName << "—" << title;
    m_pendingApp = appName;
    m_pendingTitle = title;
    m_pendingBody = body;
    m_pendingPackage = package;
    m_pendingNotification = true;
    flushPendingNotification();
}

QString NotificationDaemon::resolveAppPackage(const QString &appName,
                                              const QString &hintId) const
{
    if (!hintId.isEmpty())
        return hintId;

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
    return byName.value(appName.toLower());
}
