// SPDX-License-Identifier: BSD-3-Clause

#include "bluezmanager.h"
#include "wearablechannel.h"

#include <QtDBus>
#include <QSettings>
#include "appsettings.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>
#include <limits>
#include <cmath>

namespace {
const char BLUEZ_SERVICE[] = "org.bluez";
const char IFACE_OM[] = "org.freedesktop.DBus.ObjectManager";
const char IFACE_PROPS[] = "org.freedesktop.DBus.Properties";
const char IFACE_ADAPTER[] = "org.bluez.Adapter1";
const char IFACE_DEVICE[] = "org.bluez.Device1";
const char IFACE_GATT_SERVICE[] = "org.bluez.GattService1";
const char IFACE_GATT_CHAR[] = "org.bluez.GattCharacteristic1";

// Стандартные характеристики, читаемые для карточки устройства
const QMap<QString, QString> INFO_CHARS = {
    {QStringLiteral("00002a00-0000-1000-8000-00805f9b34fb"), QStringLiteral("deviceName")},
    {QStringLiteral("00002a24-0000-1000-8000-00805f9b34fb"), QStringLiteral("modelNumber")},
    {QStringLiteral("00002a26-0000-1000-8000-00805f9b34fb"), QStringLiteral("firmwareRevision")},
    {QStringLiteral("00002a29-0000-1000-8000-00805f9b34fb"), QStringLiteral("manufacturer")},
};
const QString BATTERY_CHAR = QStringLiteral("00002a19-0000-1000-8000-00805f9b34fb");
}

BluezManager::BluezManager(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjectMap>();

    m_resolveTimer.setInterval(500);
    connect(&m_resolveTimer, &QTimer::timeout, this, &BluezManager::onResolveTimeout);

    QSettings settings = appSettings();
    m_capabilities = settings.value(QStringLiteral("device/capabilities")).toMap();
    m_authKeyHex = settings.value(QStringLiteral("miband8/authKey")).toString();
    m_stepsGoal = settings.value(QStringLiteral("stepsGoal"), 10000).toInt();
    m_caloriesGoal = settings.value(QStringLiteral("ui/caloriesGoal"), 500).toInt();
    m_activityGoal = settings.value(QStringLiteral("ui/activityGoal"), 30).toInt();

    initAdapter();
}

void BluezManager::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void BluezManager::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    qInfo() << "[status]" << m_status;
    emit statusChanged();


    // Завершение процесса снимает его незавершённый статус
    if (m_userStatus.endsWith(QStringLiteral("…"))) {
        const bool authDone = status == QStringLiteral("Аутентификация успешна")
                && m_userStatus == QStringLiteral("Аутентификация…");
        const bool syncDone = status.startsWith(QStringLiteral("Данные обновлены"))
                && m_userStatus.startsWith(QStringLiteral("Запрос"));
        if (authDone || syncDone) {
            m_userStatus.clear();
            emit userStatusChanged();
        }
    }

    // Служебные сообщения в пользовательский статус не попадают
    static const QStringList technical = {
        QStringLiteral("Адаптер готов"),
        QStringLiteral("Auth key сохранён"),
        QStringLiteral("Сканирование остановлено"),
        QStringLiteral("Сканирование"),
        QStringLiteral("Подключено:"),
        QStringLiteral("Подключение к"),
        QStringLiteral("Аутентификация успешна"),
    };
    for (const QString &prefix : technical) {
        if (status.startsWith(prefix))
            return;
    }
    if (m_userStatus != status) {
        m_userStatus = status;
        emit userStatusChanged();
    }
}

bool BluezManager::initAdapter()
{
    const ManagedObjectMap objects = managedObjects();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        if (it.value().contains(QString::fromLatin1(IFACE_ADAPTER))) {
            m_adapterPath = it.key().path();
            const QVariantMap props = it.value().value(QString::fromLatin1(IFACE_ADAPTER));
            m_adapterPowered = props.value(QStringLiteral("Powered")).toBool();
            break;
        }
    }

    if (m_adapterPath.isEmpty()) {
        setStatus(QStringLiteral("Bluetooth-адаптер не найден"));
        return false;
    }

    subscribeSignals();
    setStatus(m_adapterPowered
                  ? QStringLiteral("Адаптер готов (%1)").arg(m_adapterPath)
                  : QStringLiteral("Bluetooth выключен — включите в настройках"));
    emit adapterPoweredChanged();
    return true;
}

void BluezManager::subscribeSignals()
{
    if (m_signalsSubscribed)
        return;
    QDBusConnection bus = QDBusConnection::systemBus();
    bool ok = true;
    ok &= bus.connect(QString::fromLatin1(BLUEZ_SERVICE), QString(),
                      QString::fromLatin1(IFACE_OM), QStringLiteral("InterfacesAdded"),
                      this, SLOT(onInterfacesAdded(QDBusObjectPath,InterfaceMap)));
    ok &= bus.connect(QString::fromLatin1(BLUEZ_SERVICE), QString(),
                      QString::fromLatin1(IFACE_OM), QStringLiteral("InterfacesRemoved"),
                      this, SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    // Пустой путь — ловим PropertiesChanged от всех объектов BlueZ
    ok &= bus.connect(QString::fromLatin1(BLUEZ_SERVICE), QString(),
                      QString::fromLatin1(IFACE_PROPS), QStringLiteral("PropertiesChanged"),
                      this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    m_signalsSubscribed = ok;
    if (!ok)
        qWarning() << "Не удалось подписаться на сигналы BlueZ";
}

ManagedObjectMap BluezManager::managedObjects() const
{
    QDBusInterface om(QString::fromLatin1(BLUEZ_SERVICE), QStringLiteral("/"),
                      QString::fromLatin1(IFACE_OM), QDBusConnection::systemBus());
    QDBusReply<ManagedObjectMap> reply = om.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        qWarning() << "GetManagedObjects:" << reply.error().message();
        return ManagedObjectMap();
    }
    return reply.value();
}

void BluezManager::startScan()
{
    if (m_adapterPath.isEmpty() && !initAdapter())
        return;
    if (!m_adapterPowered) {
        setStatus(QStringLiteral("Bluetooth выключен — включите в настройках"));
        return;
    }

    QDBusInterface adapter(QString::fromLatin1(BLUEZ_SERVICE), m_adapterPath,
                           QString::fromLatin1(IFACE_ADAPTER), QDBusConnection::systemBus());

    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    QDBusReply<void> filterReply = adapter.call(QStringLiteral("SetDiscoveryFilter"),
                                                QVariant::fromValue(filter));
    if (!filterReply.isValid())
        qWarning() << "SetDiscoveryFilter:" << filterReply.error().message();

    QDBusReply<void> reply = adapter.call(QStringLiteral("StartDiscovery"));
    if (!reply.isValid()) {
        setStatus(QStringLiteral("Ошибка сканирования: %1").arg(reply.error().message()));
        return;
    }

    updateFromManagedObjects();
    m_scanning = true;
    emit scanningChanged();
    setStatus(QStringLiteral("Сканирование…"));
}

void BluezManager::stopScan()
{
    if (!m_adapterPath.isEmpty()) {
        QDBusInterface adapter(QString::fromLatin1(BLUEZ_SERVICE), m_adapterPath,
                               QString::fromLatin1(IFACE_ADAPTER), QDBusConnection::systemBus());
        adapter.asyncCall(QStringLiteral("StopDiscovery"));
    }
    m_scanning = false;
    emit scanningChanged();
    setStatus(QStringLiteral("Сканирование остановлено"));
}

void BluezManager::updateFromManagedObjects()
{
    const ManagedObjectMap objects = managedObjects();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap deviceProps = it.value().value(QString::fromLatin1(IFACE_DEVICE));
        if (!deviceProps.isEmpty())
            m_devices.upsert(it.key().path(), deviceProps);
    }
}

void BluezManager::onInterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &interfaces)
{
    const QVariantMap deviceProps = interfaces.value(QString::fromLatin1(IFACE_DEVICE));
    if (deviceProps.isEmpty())
        return;
    m_devices.upsert(path.path(), deviceProps);

    // Ждали появления устройства в сканировании — подключаемся
    if (m_waitingForDevice && path.path() == m_pendingPath) {
        m_waitingForDevice = false;
        stopScan();
        doConnect();
    }
}

void BluezManager::onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    if (interfaces.contains(QString::fromLatin1(IFACE_DEVICE)))
        m_devices.remove(path.path());
}

void BluezManager::onPropertiesChanged(const QString &interface, const QVariantMap &props,
                                       const QStringList &invalidated, const QDBusMessage &message)
{
    Q_UNUSED(invalidated);

    if (interface == QString::fromLatin1(IFACE_ADAPTER)) {
        if (props.contains(QStringLiteral("Powered"))) {
            m_adapterPowered = props.value(QStringLiteral("Powered")).toBool();
            emit adapterPoweredChanged();
            if (!m_adapterPowered)
                stopScan();
        }
        if (props.contains(QStringLiteral("Discovering"))) {
            m_scanning = props.value(QStringLiteral("Discovering")).toBool();
            emit scanningChanged();
        }
        return;
    }

    if (interface == QString::fromLatin1(IFACE_GATT_CHAR)) {
        if (m_channel && props.contains(QStringLiteral("Value")))
            m_channel->onCharacteristicValue(message.path(),
                                             props.value(QStringLiteral("Value")).toByteArray());
        return;
    }

    if (interface != QString::fromLatin1(IFACE_DEVICE))
        return;

    const QString path = message.path();
    m_devices.upsert(path, props);

    // Завершение отложенного подключения: дождались ServicesResolved
    if (path == m_pendingPath && props.value(QStringLiteral("ServicesResolved")).toBool()) {
        m_resolveTimer.stop();
        finishConnect();
        return;
    }

    // Обрыв соединения с браслетом
    if (path == m_pendingPath && props.contains(QStringLiteral("Connected"))
            && !props.value(QStringLiteral("Connected")).toBool()
            && !m_connectedAddress.isEmpty()) {
        if (m_channel) {
            delete m_channel;
            m_channel = nullptr;
        }
        m_resolveTimer.stop();
        m_pendingPath.clear();
        m_connectedAddress.clear();
        m_bandInfo.remove(QStringLiteral("heartRate"));
        m_authStatus.clear();
        emit authStatusChanged();
        emit bandReadyChanged();
        emit bandInfoChanged();
        emit servicesChanged();
        emit connectedAddressChanged();
        setStatus(QStringLiteral("Соединение разорвано"));
        setBusy(false);
        emit bandDisconnected();
    }
}

QString BluezManager::devicePathForAddress(const QString &address)
{
    QString a = address;
    a.replace(QLatin1Char(':'), QLatin1Char('_'));
    return QStringLiteral("/org/bluez/hci0/dev_%1").arg(a);
}

bool BluezManager::deviceKnown(const QString &path) const
{
    const ManagedObjectMap objects = managedObjects();
    const auto it = objects.constFind(QDBusObjectPath(path));
    return it != objects.constEnd()
            && it.value().contains(QString::fromLatin1(IFACE_DEVICE));
}

void BluezManager::connectToBand(const QString &address)
{
    if (m_channel) {
        delete m_channel;
        m_channel = nullptr;
    }
    m_authStatus.clear();
    emit authStatusChanged();
    emit bandReadyChanged();
    const QString path = devicePathForAddress(address);
    m_pendingPath = path;

    // Запоминаем последний MAC — демон берёт его из QSettings
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("device/lastAddress"), address);
    m_services.clear();
    emit servicesChanged();
    m_bandInfo.clear();
    emit bandInfoChanged();

    if (!deviceKnown(path)) {
        // Устройство не в кэше BlueZ — ищем его сканированием
        setStatus(QStringLiteral("Поиск %1…").arg(address));
        m_waitingForDevice = true;
        if (!m_scanning)
            startScan();
        return;
    }
    doConnect();
}

void BluezManager::doConnect()
{
    QDBusInterface *dev = new QDBusInterface(QString::fromLatin1(BLUEZ_SERVICE), m_pendingPath,
                                             QString::fromLatin1(IFACE_DEVICE),
                                             QDBusConnection::systemBus(), this);
    if (dev->property("Connected").toBool()) {
        if (dev->property("ServicesResolved").toBool()) {
            finishConnect();
        } else {
            setStatus(QStringLiteral("Ожидание сервисов…"));
            m_resolveAttempts = 0;
            m_resolveTimer.start();
        }
        return;
    }

    setStatus(QStringLiteral("Подключение к %1…").arg(m_pendingPath));
    QDBusPendingCall call = dev->asyncCall(QStringLiteral("Connect"));
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<void> reply = *w;
        if (reply.isError()) {
            m_pendingPath.clear();
            const QString msg = reply.error().message();
            setStatus(QStringLiteral("Ошибка подключения: %1").arg(msg));
            setBusy(false);
            emit deviceError(msg);
            return;
        }
        setStatus(QStringLiteral("Ожидание сервисов…"));
        m_resolveAttempts = 0;
        m_resolveTimer.start();
    });
}

void BluezManager::onResolveTimeout()
{
    if (m_pendingPath.isEmpty()) {
        m_resolveTimer.stop();
        return;
    }
    QDBusInterface dev(QString::fromLatin1(BLUEZ_SERVICE), m_pendingPath,
                       QString::fromLatin1(IFACE_DEVICE), QDBusConnection::systemBus());
    if (dev.property("ServicesResolved").toBool()) {
        m_resolveTimer.stop();
        finishConnect();
        return;
    }
    if (++m_resolveAttempts > 40) {
        m_resolveTimer.stop();
        const QString msg = QStringLiteral("Тайм-аут ожидания GATT-сервисов");
        setStatus(msg);
        setBusy(false);
        emit deviceError(msg);
    }
}

void BluezManager::finishConnect()
{
    if (m_pendingPath.isEmpty() || m_channel)
        return;

    QDBusInterface dev(QString::fromLatin1(BLUEZ_SERVICE), m_pendingPath,
                       QString::fromLatin1(IFACE_DEVICE), QDBusConnection::systemBus());
    m_connectedAddress = dev.property("Address").toString();
    emit connectedAddressChanged();

    enumerateServices();
    readBandInfo();
    setupWearableChannel();

    setStatus(QStringLiteral("Подключено: %1").arg(m_connectedAddress));
    emit servicesChanged();
    emit deviceReady();
}

void BluezManager::enumerateServices()
{
    m_services.clear();
    const ManagedObjectMap objects = managedObjects();
    const QString prefix = m_pendingPath + QLatin1Char('/');

    QMap<QString, QVariantMap> serviceNodes;   // путь -> props
    QMap<QString, QVariantMap> charNodes;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        if (!path.startsWith(prefix))
            continue;
        const QVariantMap serviceProps = it.value().value(QString::fromLatin1(IFACE_GATT_SERVICE));
        if (!serviceProps.isEmpty())
            serviceNodes.insert(path, serviceProps);
        const QVariantMap charProps = it.value().value(QString::fromLatin1(IFACE_GATT_CHAR));
        if (!charProps.isEmpty())
            charNodes.insert(path, charProps);
    }

    for (auto sit = serviceNodes.constBegin(); sit != serviceNodes.constEnd(); ++sit) {
        QVariantMap service;
        const QString uuid = sit.value().value(QStringLiteral("UUID")).toString();
        service.insert(QStringLiteral("uuid"), uuid);
        service.insert(QStringLiteral("primary"),
                       sit.value().value(QStringLiteral("Primary")).toBool());

        QVariantList chars;
        for (auto cit = charNodes.constBegin(); cit != charNodes.constEnd(); ++cit) {
            if (cit.value().value(QStringLiteral("Service")).value<QDBusObjectPath>().path() != sit.key())
                continue;
            QVariantMap c;
            c.insert(QStringLiteral("uuid"), cit.value().value(QStringLiteral("UUID")).toString());
            c.insert(QStringLiteral("flags"), cit.value().value(QStringLiteral("Flags")).toStringList());
            c.insert(QStringLiteral("path"), cit.key());
            chars.append(c);
        }
        service.insert(QStringLiteral("characteristics"), chars);
        m_services.append(service);
    }
    emit servicesChanged();
    qInfo() << "Сервисов:" << m_services.size();
}

QString BluezManager::readStringChar(const QString &charPath)
{
    QDBusInterface chrc(QString::fromLatin1(BLUEZ_SERVICE), charPath,
                        QString::fromLatin1(IFACE_GATT_CHAR), QDBusConnection::systemBus());
    QVariantMap options;
    QDBusReply<QByteArray> reply = chrc.call(QStringLiteral("ReadValue"),
                                             QVariant::fromValue(options));
    if (!reply.isValid()) {
        qWarning() << "ReadValue" << charPath << ":" << reply.error().message();
        return QString();
    }
    return QString::fromUtf8(reply.value());
}

void BluezManager::readBandInfo()
{
    m_bandInfo.clear();

    // Находим пути характеристик по UUID из уже построенного дерева сервисов
    QMap<QString, QString> pathByUuid;
    for (const QVariant &s : m_services) {
        const QVariantList chars = s.toMap().value(QStringLiteral("characteristics")).toList();
        for (const QVariant &c : chars)
            pathByUuid.insert(c.toMap().value(QStringLiteral("uuid")).toString(),
                              c.toMap().value(QStringLiteral("path")).toString());
    }

    for (auto it = INFO_CHARS.constBegin(); it != INFO_CHARS.constEnd(); ++it) {
        if (!pathByUuid.contains(it.key()))
            continue;
        const QString value = readStringChar(pathByUuid.value(it.key()));
        if (!value.isEmpty())
            m_bandInfo.insert(it.value(), value);
    }

    if (pathByUuid.contains(BATTERY_CHAR)) {
        QDBusInterface chrc(QString::fromLatin1(BLUEZ_SERVICE), pathByUuid.value(BATTERY_CHAR),
                            QString::fromLatin1(IFACE_GATT_CHAR), QDBusConnection::systemBus());
        QVariantMap options;
        QDBusReply<QByteArray> reply = chrc.call(QStringLiteral("ReadValue"),
                                                 QVariant::fromValue(options));
        if (reply.isValid() && !reply.value().isEmpty())
            m_bandInfo.insert(QStringLiteral("batteryLevel"),
                              static_cast<quint8>(reply.value().at(0)));
    }

    emit bandInfoChanged();
}

void BluezManager::setAuthKey(const QString &hexKey)
{
    QString hex = hexKey.trimmed();
    if (hex.startsWith(QStringLiteral("0x")))
        hex = hex.mid(2);
    m_authKeyHex = hex;
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("miband8/authKey"), hex);
    setStatus(QStringLiteral("Auth key сохранён"));
}

void BluezManager::startBandAuth()
{
    if (!m_channel || !m_channel->requiresAuth()) {
        setStatus(QStringLiteral("Устройству не требуется ключ или оно не подключено"));
        return;
    }
    const QByteArray key = QByteArray::fromHex(m_authKeyHex.toLatin1());
    if (key.size() != 16) {
        setStatus(QStringLiteral("Неверный auth key: нужно 32 hex-символа"));
        return;
    }
    m_channel->setAuthKey(key);
    m_channel->startAuth();
}

void BluezManager::setupWearableChannel()
{
    m_channel = createWearableChannel(m_services, this);
    if (!m_channel) {
        setBusy(false);
        return;
    }
    const QString storageId = m_channel->storageId(m_connectedAddress);
    m_capabilities = m_channel->capabilities();
    appSettings().setValue(QStringLiteral("device/capabilities"), m_capabilities);
    m_storage.selectDevice(storageId);
    m_storage.setLiveEstimation(estimatedCalories(), estimatedActivity());
    emit capabilitiesChanged();
    appSettings().setValue(QStringLiteral("device/storageId"), storageId);
    connect(m_channel, &WearableChannel::readyChanged, this, [this]() {
        if (m_channel->ready()) {
            if (!m_channel->requiresAuth()) {
                m_authStatus = QStringLiteral("Подключено");
                emit authStatusChanged();
                setStatus(m_authStatus);
            }
            setBusy(false);
        }
        emit bandReadyChanged();
    });
    connect(m_channel, &WearableChannel::stepsReceived, this, [this](quint32 steps) {
        if (steps > quint32(std::numeric_limits<int>::max()))
            return;
        m_storage.saveLiveReading(int(steps), -1);
    });
    connect(m_channel, &WearableChannel::heartRateReceived, this, [this](int bpm) {
        m_bandInfo.insert(QStringLiteral("heartRate"), bpm);
        m_storage.saveLiveReading(-1, bpm);
        emit bandInfoChanged();
    });
    connect(m_channel, &WearableChannel::error, this, [this](const QString &message) {
        setBusy(false);
        setStatus(message);
        emit deviceError(message);
    });
    connect(m_channel, &WearableChannel::authStatusChanged, this, [this](const QString &s) {
        m_authStatus = s;
        emit authStatusChanged();
        setStatus(s);
    });
    connect(m_channel, &WearableChannel::authFailed, this, [this](const QString &s) {
        m_authStatus = s;
        emit authStatusChanged();
        setStatus(s);
        setBusy(false);
        emit deviceError(s);
    });
    connect(m_channel, &WearableChannel::batteryReceived, this, [this](int level, int state) {
        m_bandInfo.insert(QStringLiteral("batteryLevel"), level);
        m_bandInfo.insert(QStringLiteral("batteryState"), state);
        m_storage.saveBattery(level, state);
        emit bandInfoChanged();
        setBusy(false); // auth завершена; дальше синк крутит свой индикатор
        emit bandBatteryReceived(level, state);
    });
    connect(m_channel, &WearableChannel::deviceInfoReceived, this,
            [this](const QString &serial, const QString &firmware, const QString &model) {
        if (!serial.isEmpty())
            m_bandInfo.insert(QStringLiteral("serialNumber"), serial);
        if (!firmware.isEmpty())
            m_bandInfo.insert(QStringLiteral("firmwareRevision"), firmware);
        if (!model.isEmpty())
            m_bandInfo.insert(QStringLiteral("modelNumber"), model);
        emit bandInfoChanged();
    });
    connect(m_channel, &WearableChannel::activityFileParsed, this, [this](const QVariantMap &data) {
        m_activityResults.append(data);
        m_storage.saveParsed(data);
        emit activityResultsChanged();
    });
    connect(m_channel, &WearableChannel::activityFetchProgress, this,
            [this](const QString &s) { setStatus(s); });
    connect(m_channel, &WearableChannel::activityFetchFinished, this,
            [this]() {
        setStatus(QStringLiteral("Данные обновлены"));
        QSettings settings = appSettings();
        settings.setValue(QStringLiteral("device/lastSyncTime"),
                          QDateTime::currentDateTime().toString(Qt::ISODate));
        emit activitySyncFinished();
        notifyGoalsAchieved();
    });

    m_channel->start();
    if (m_channel->requiresAuth() && !m_authKeyHex.isEmpty())
        startBandAuth();
}

void BluezManager::notifyGoalsAchieved()
{
    // Системное уведомление при первом достижении каждой цели за день
    const QVariantMap t = m_storage.todaySummary();
    if (t.isEmpty())
        return;

    QSettings settings = appSettings();
    settings.sync(); // цели меняет отдельный GUI-процесс
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QString rec = settings.value(QStringLiteral("goals/notified")).toString();
    if (rec.section(QLatin1Char(':'), 0, 0) != today)
        rec = today + QLatin1Char(':');
    QStringList fired = rec.section(QLatin1Char(':'), 1)
            .split(QLatin1Char(','), QString::SkipEmptyParts);

    struct Check { QString key; QString id; int goal; QString text; };
    const QList<Check> checks = {
        {QStringLiteral("steps"), QStringLiteral("steps"), settings.value(QStringLiteral("stepsGoal"), 10000).toInt(),
         QStringLiteral("%1 шагов")},
        {QStringLiteral("calories"), QStringLiteral("kcal"), settings.value(QStringLiteral("ui/caloriesGoal"), 500).toInt(),
         QStringLiteral("%1 ккал")},
        {QStringLiteral("activityMin"), QStringLiteral("activity"), settings.value(QStringLiteral("ui/activityGoal"), 30).toInt(),
         QStringLiteral("%1 мин активности")},
    };
    for (const Check &c : checks) {
        const qlonglong v = t.value(c.key).toLongLong();
        if (c.goal <= 0 || v < c.goal || fired.contains(c.id))
            continue;
        sendSystemNotification(QStringLiteral("Цель достигнута!"), c.text.arg(v));
        fired << c.id;
    }
    settings.setValue(QStringLiteral("goals/notified"),
                      today + QLatin1Char(':') + fired.join(QLatin1Char(',')));
}

void BluezManager::sendSystemNotification(const QString &summary, const QString &body)
{
    // appName = наш пакет — демон такие уведомления на браслет НЕ пересылает
    QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("/org/freedesktop/Notifications"),
                QStringLiteral("org.freedesktop.Notifications"),
                QStringLiteral("Notify"));
    QVariantMap hints;
    hints.insert(QStringLiteral("x-aurora-application-id"),
                 QStringLiteral("ru.nighteugene.aurorafitness"));
    msg << QStringLiteral("ru.nighteugene.aurorafitness") << uint(0)
        << QStringLiteral("ru.nighteugene.aurorafitness") << summary << body
        << QStringList() << hints << int(-1);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void BluezManager::syncActivity()
{
    if (!m_channel || !m_channel->ready()) {
        setStatus(QStringLiteral("Сначала подключитесь и авторизуйтесь"));
        return;
    }
    m_activityResults.clear();
    emit activityResultsChanged();
    emit activitySyncStarted();
    m_channel->sync();
}

void BluezManager::sendTestNotification(const QString &title, const QString &body,
                                        const QString &appName, const QString &package)
{
    if (!m_channel || !m_channel->ready()) {
        setStatus(QStringLiteral("Сначала подключитесь и авторизуйтесь"));
        return;
    }
    m_channel->sendNotification(appName, title, body, package);
    setStatus(QStringLiteral("Уведомление отправлено"));
}

bool BluezManager::bandReady() const
{
    return m_channel && m_channel->ready();
}

bool BluezManager::requiresAuth() const
{
    return m_channel && m_channel->requiresAuth();
}

double BluezManager::weightKg() const
{
    return appSettings().value(QStringLiteral("profile/weightKg"), 70.0).toDouble();
}

int BluezManager::heightCm() const
{
    return appSettings().value(QStringLiteral("profile/heightCm"), 170).toInt();
}

void BluezManager::setWeightKg(double value)
{
    if (!std::isfinite(value) || value < 20 || value > 300 || value == weightKg())
        return;
    appSettings().setValue(QStringLiteral("profile/weightKg"), value);
    m_storage.recalculateCalories();
    emit profileChanged();
}

void BluezManager::setHeightCm(int value)
{
    if (value < 80 || value > 250 || value == heightCm())
        return;
    appSettings().setValue(QStringLiteral("profile/heightCm"), value);
    m_storage.recalculateCalories();
    emit profileChanged();
}

bool BluezManager::powerOnAdapter()
{
    if (m_adapterPath.isEmpty() && !initAdapter())
        return false;
    QDBusInterface props(QString::fromLatin1(BLUEZ_SERVICE), m_adapterPath,
                         QString::fromLatin1(IFACE_PROPS), QDBusConnection::systemBus());
    QDBusReply<void> reply = props.call(QStringLiteral("Set"),
                                        QString::fromLatin1(IFACE_ADAPTER),
                                        QStringLiteral("Powered"),
                                        QVariant::fromValue(QDBusVariant(true)));
    if (!reply.isValid()) {
        qWarning() << "Powered=true:" << reply.error().message();
        return false;
    }
    m_adapterPowered = true;
    emit adapterPoweredChanged();
    return true;
}

void BluezManager::setDaemonEnabled(bool enabled)
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("daemon/notifyEnabled"), enabled);

    settings.sync();
    // Служба обслуживает GUI и BLE независимо от пересылки уведомлений.
    // Регистрацией службы при запуске GUI занимается BandProxy.
}

bool BluezManager::daemonEnabled() const
{
    const QSettings settings = appSettings();
    return settings.value(QStringLiteral("daemon/notifyEnabled"), false).toBool();
}

void BluezManager::setDaemonSyncEnabled(bool enabled)
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("daemon/syncEnabled"), enabled);
    if (enabled && settings.value(QStringLiteral("daemon/syncIntervalMin"), 0).toInt() <= 0)
        settings.setValue(QStringLiteral("daemon/syncIntervalMin"), 30);
}

bool BluezManager::daemonSyncEnabled() const
{
    const QSettings settings = appSettings();
    return settings.value(QStringLiteral("daemon/syncEnabled"), false).toBool();
}

void BluezManager::autoConnectLast()
{
    const QSettings settings = appSettings();
    const QString address = settings.value(QStringLiteral("device/lastAddress"), settings.value(QStringLiteral("miband8/lastAddress"))).toString();
    if (address.isEmpty())
        return;

    if (!m_adapterPowered)
        powerOnAdapter(); // не страшно, если не выйдет — статус покажет проблему

    // Разовый автосинк после первой успешной auth (battery приходит сразу после неё)
    QMetaObject::Connection *conn = new QMetaObject::Connection;
    *conn = connect(this, &BluezManager::bandReadyChanged, this, [this, conn]() {
        if (!bandReady())
            return;
        disconnect(*conn);
        delete conn;
        syncActivity();
    });
    connectToBandWhenFree(address);
}

bool BluezManager::bandLinkActive(const QString &address) const
{
    QDBusInterface dev(QString::fromLatin1(BLUEZ_SERVICE), devicePathForAddress(address),
                       QString::fromLatin1(IFACE_DEVICE), QDBusConnection::systemBus());
    return dev.property("Connected").toBool();
}

void BluezManager::connectToBandWhenFree(const QString &address, int attempt)
{
    if (attempt == 0)
        setBusy(true); // индикатор сразу, пока ждём освобождения линка
    // Disconnect демона асинхронен: если линк ещё поднят, подождём,
    // иначе его обрыв попадёт в середину нашего Connect
    if (attempt < 16 && bandLinkActive(address)) {
        QTimer::singleShot(500, this,
                           [this, address, attempt]() { connectToBandWhenFree(address, attempt + 1); });
        return;
    }
    connectToBand(address);
}

QString BluezManager::lastSyncTimeText() const
{
    const QSettings settings = appSettings();
    const QDateTime t = QDateTime::fromString(
                settings.value(QStringLiteral("device/lastSyncTime"), settings.value(QStringLiteral("miband8/lastSyncTime"))).toString(), Qt::ISODate);
    if (!t.isValid())
        return QString();
    if (t.date() == QDate::currentDate())
        return t.toString(QStringLiteral("HH:mm"));
    return t.toString(QStringLiteral("dd.MM HH:mm"));
}

QString BluezManager::connectedDeviceName() const
{
    // Краткое имя: "Xiaomi Smart Band 8 1902" -> "Mi Band 8"
    auto shorten = [](QString n) {
        n.replace(QStringLiteral("Xiaomi Smart Band"), QStringLiteral("Mi Band"));
        n.remove(QRegularExpression(QStringLiteral("\\s+[0-9A-Fa-f]{4}$"))); // хвост MAC
        return n.trimmed();
    };
    const QString fromInfo = m_bandInfo.value(QStringLiteral("deviceName")).toString();
    if (!fromInfo.isEmpty())
        return shorten(fromInfo);
    if (!m_pendingPath.isEmpty()) {
        QDBusInterface dev(QString::fromLatin1(BLUEZ_SERVICE), m_pendingPath,
                           QString::fromLatin1(IFACE_DEVICE), QDBusConnection::systemBus());
        const QString alias = dev.property("Alias").toString();
        if (!alias.isEmpty())
            return shorten(alias);
        const QString name = dev.property("Name").toString();
        if (!name.isEmpty())
            return shorten(name);
    }
    return m_connectedAddress;
}

static const QStringList &cardIds()
{
    static const QStringList ids = {QStringLiteral("steps"), QStringLiteral("calories"),
                                    QStringLiteral("activity"), QStringLiteral("hr"),
                                    QStringLiteral("sleep"), QStringLiteral("stress"),
                                    QStringLiteral("spo2"), QStringLiteral("battery")};
    return ids;
}

QVariantMap BluezManager::cardVisibility() const
{
    QVariantMap out;
    const QSettings settings = appSettings();
    for (const QString &id : cardIds())
        out.insert(id, settings.value(QStringLiteral("view/card/") + id, true).toBool());
    return out;
}

void BluezManager::setCardVisible(const QString &id, bool visible)
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("view/card/") + id, visible);
    emit viewConfigChanged();
}

QStringList BluezManager::cardOrder() const
{
    const QString saved = appSettings().value(QStringLiteral("view/order")).toString();
    QStringList order = saved.split(QLatin1Char(','), QString::SkipEmptyParts);
    // выкидываем неизвестные, добиваем новые в конец
    QStringList filtered;
    for (const QString &id : order) {
        if (cardIds().contains(id) && !filtered.contains(id))
            filtered << id;
    }
    for (const QString &id : cardIds()) {
        if (!filtered.contains(id))
            filtered << id;
    }
    return filtered;
}

void BluezManager::moveCard(const QString &id, int dir)
{
    QStringList order = cardOrder();
    const int i = order.indexOf(id);
    const int j = i + dir;
    if (i < 0 || j < 0 || j >= order.size())
        return;
    order.swap(i, j);
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("view/order"), order.join(QLatin1Char(',')));
    emit viewConfigChanged();
}

void BluezManager::setCardOrder(const QStringList &order)
{
    QStringList full;
    for (const QString &id : order) {
        if (cardIds().contains(id) && !full.contains(id))
            full << id;
    }
    for (const QString &id : cardIds()) {
        if (!full.contains(id))
            full << id;
    }
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("view/order"), full.join(QLatin1Char(',')));
    emit viewConfigChanged();
}

void BluezManager::setStepsGoal(int goal)
{    if (goal <= 0 || goal == m_stepsGoal)
        return;
    m_stepsGoal = goal;
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("stepsGoal"), goal);
    emit stepsGoalChanged();
}

void BluezManager::setCaloriesGoal(int goal)
{
    if (goal <= 0 || goal == m_caloriesGoal)
        return;
    m_caloriesGoal = goal;
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("ui/caloriesGoal"), goal);
    emit caloriesGoalChanged();
}

void BluezManager::setActivityGoal(int goal)
{
    if (goal <= 0 || goal == m_activityGoal)
        return;
    m_activityGoal = goal;
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("ui/activityGoal"), goal);
    emit activityGoalChanged();
}

void BluezManager::disconnectBand()
{
    if (m_channel) {
        delete m_channel;
        m_channel = nullptr;
    }
    if (!m_pendingPath.isEmpty()) {
        QDBusInterface dev(QString::fromLatin1(BLUEZ_SERVICE), m_pendingPath,
                           QString::fromLatin1(IFACE_DEVICE), QDBusConnection::systemBus());
        dev.asyncCall(QStringLiteral("Disconnect"));
    }
    m_pendingPath.clear();
    m_resolveTimer.stop();
    m_waitingForDevice = false;
    m_bandInfo.remove(QStringLiteral("heartRate"));
    m_authStatus.clear();
    emit authStatusChanged();
    emit bandReadyChanged();
    emit bandInfoChanged();
    emit servicesChanged();
    setBusy(false);
    m_connectedAddress.clear();
    emit connectedAddressChanged();
    setStatus(QStringLiteral("Отключено"));
}

void BluezManager::cliScanFinished()
{
    stopScan();
    qInfo() << "=== Найденные устройства ===";
    const ManagedObjectMap objects = managedObjects();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap props = it.value().value(QString::fromLatin1(IFACE_DEVICE));
        if (props.isEmpty())
            continue;
        qInfo() << props.value(QStringLiteral("Name")).toString()
                << props.value(QStringLiteral("Address")).toString()
                << "RSSI" << props.value(QStringLiteral("RSSI")).toInt();
    }
}

void BluezManager::cliReadFinished()
{
    qInfo() << "=== bandInfo ===" << m_bandInfo;
    for (const QVariant &s : m_services) {
        const QVariantMap service = s.toMap();
        qInfo() << "service" << service.value(QStringLiteral("uuid")).toString();
        const QVariantList chars = service.value(QStringLiteral("characteristics")).toList();
        for (const QVariant &c : chars)
            qInfo() << "  char" << c.toMap().value(QStringLiteral("uuid")).toString()
                    << c.toMap().value(QStringLiteral("flags")).toStringList();
    }
    disconnectBand();
}
