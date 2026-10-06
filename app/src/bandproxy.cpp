// SPDX-License-Identifier: BSD-3-Clause

#include "bandproxy.h"
#include "appsettings.h"
#include "storage.h"

#include <QDBusArgument>
#include <QDBusVariant>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QSettings>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QDebug>
#include <cmath>

namespace {
const char BAND_SERVICE[] = "ru.nighteugene.aurorafitness.band";
const char BAND_PATH[] = "/band";
const char DAEMON_UNIT[] = "ru.nighteugene.aurorafitness-daemon.service";

// Вложенные контейнеры a{sv}/av приходят как QDBusArgument, включая
// карты внутри списков сервисов и характеристик.
QVariant unpackDBus(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusVariant>())
        return unpackDBus(qvariant_cast<QDBusVariant>(value).variant());
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument arg = qvariant_cast<QDBusArgument>(value);
        if (arg.currentType() == QDBusArgument::MapType)
            return unpackDBus(qdbus_cast<QVariantMap>(arg));
        if (arg.currentType() == QDBusArgument::ArrayType) {
            if (arg.currentSignature() == QStringLiteral("as"))
                return qdbus_cast<QStringList>(arg);
            return unpackDBus(qdbus_cast<QVariantList>(arg));
        }
        return value;
    }
    if (value.type() == QVariant::Map) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = unpackDBus(it.value());
        return map;
    }
    if (value.type() == QVariant::List) {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = unpackDBus(item);
        return list;
    }
    return value;
}


const QStringList &cardIds()
{
    static const QStringList ids = {QStringLiteral("steps"), QStringLiteral("calories"),
                                    QStringLiteral("activity"), QStringLiteral("hr"),
                                    QStringLiteral("sleep"), QStringLiteral("stress"),
                                    QStringLiteral("spo2"), QStringLiteral("battery")};
    return ids;
}
}

BandProxy::BandProxy(Storage *storage, QObject *parent)
    : QObject(parent), m_storage(storage)
{
    QSettings settings = appSettings();
    m_stepsGoal = settings.value(QStringLiteral("stepsGoal"), 10000).toInt();
    m_caloriesGoal = settings.value(QStringLiteral("ui/caloriesGoal"), 500).toInt();
    m_activityGoal = settings.value(QStringLiteral("ui/activityGoal"), 30).toInt();

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(BAND_SERVICE), QString::fromLatin1(BAND_PATH),
                QString::fromLatin1(BAND_SERVICE), QStringLiteral("stateChanged"),
                this, SLOT(onStateChanged(QVariantMap)));
    bus.connect(QString::fromLatin1(BAND_SERVICE), QString::fromLatin1(BAND_PATH),
                QString::fromLatin1(BAND_SERVICE), QStringLiteral("activitySyncStarted"),
                this, SIGNAL(activitySyncStarted()));
    bus.connect(QString::fromLatin1(BAND_SERVICE), QString::fromLatin1(BAND_PATH),
                QString::fromLatin1(BAND_SERVICE), QStringLiteral("activitySyncFinished"),
                this, SIGNAL(activitySyncFinished()));
    bus.connect(QString::fromLatin1(BAND_SERVICE), QString::fromLatin1(BAND_PATH),
                QString::fromLatin1(BAND_SERVICE), QStringLiteral("deviceError"),
                this, SIGNAL(deviceError(QString)));

    m_watcher = new QDBusServiceWatcher(QString::fromLatin1(BAND_SERVICE), bus,
                                        QDBusServiceWatcher::WatchForRegistration
                                        | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &BandProxy::onServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &BandProxy::onServiceUnregistered);

    if (bus.interface()->isServiceRegistered(QString::fromLatin1(BAND_SERVICE))) {
        refreshState();
    } else {
        setOffline(tr("Служба браслета не запущена"));
        tryStartDaemon();
    }
}

void BandProxy::callDaemon(const QString &method,
                           const QVariant &a1, const QVariant &a2,
                           const QVariant &a3, const QVariant &a4)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
                QString::fromLatin1(BAND_SERVICE), QString::fromLatin1(BAND_PATH),
                QString::fromLatin1(BAND_SERVICE), method);
    QVariantList args;
    for (const QVariant &a : {a1, a2, a3, a4}) {
        if (!a.isValid())
            break;
        args << a;
    }
    msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg);
}

void BandProxy::refreshState()
{
    QDBusInterface iface(QString::fromLatin1(BAND_SERVICE),
                         QString::fromLatin1(BAND_PATH),
                         QString::fromLatin1(BAND_SERVICE),
                         QDBusConnection::sessionBus());
    QDBusPendingCall call = iface.asyncCall(QStringLiteral("getState"));
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &BandProxy::onGetStateFinished);
}

void BandProxy::onGetStateFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    const QDBusPendingReply<QVariantMap> reply = *watcher;
    if (!reply.isValid()) {
        setOffline(tr("Служба браслета не отвечает"));
        return;
    }
    onStateChanged(reply.value());
}

void BandProxy::onServiceRegistered()
{
    m_haveDataRevision = false;
    refreshState();
}

void BandProxy::onServiceUnregistered()
{
    setOffline(tr("Служба браслета не запущена"));
}

void BandProxy::setOffline(const QString &reason)
{
    const bool wasReady = m_ready;
    const bool hadAddress = !m_connectedAddress.isEmpty();
    const bool wasBusy = m_busy;
    m_ready = false;
    m_busy = false;
    m_connectedAddress.clear();
    m_connectedDeviceName.clear();
    m_heartRate = -1;
    m_status = reason;
    m_userStatus = reason;
    emit statusChanged();
    emit userStatusChanged();
    if (wasReady)
        emit bandReadyChanged();
    if (wasBusy)
        emit busyChanged();
    if (hadAddress) {
        emit connectedAddressChanged();
        emit bandInfoChanged();
    }
}

void BandProxy::onStateChanged(const QVariantMap &state)
{
    const QVariantMap s = unpackDBus(state).toMap();
    const bool scanning = s.value(QStringLiteral("scanning")).toBool();
    if (m_scanning != scanning) {
        m_scanning = scanning;
        emit scanningChanged();
    }
    const bool powered = s.value(QStringLiteral("adapterPowered")).toBool();
    if (m_adapterPowered != powered) {
        m_adapterPowered = powered;
        emit adapterPoweredChanged();
    }
    const QString status = s.value(QStringLiteral("status")).toString();
    if (m_status != status) {
        m_status = status;
        emit statusChanged();
    }
    const QString userStatus = s.value(QStringLiteral("userStatus")).toString();
    if (m_userStatus != userStatus) {
        m_userStatus = userStatus;
        emit userStatusChanged();
    }
    const QString authStatus = s.value(QStringLiteral("authStatus")).toString();
    if (m_authStatus != authStatus) {
        m_authStatus = authStatus;
        emit authStatusChanged();
    }
    const bool busy = s.value(QStringLiteral("busy")).toBool();
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
    const bool ready = s.value(QStringLiteral("ready")).toBool();
    if (m_ready != ready) {
        m_ready = ready;
        emit bandReadyChanged();
    }
    const bool requiresAuth = s.value(QStringLiteral("requiresAuth")).toBool();
    const QVariantList services = s.value(QStringLiteral("services")).toList();
    if (m_requiresAuth != requiresAuth || m_services != services) {
        m_requiresAuth = requiresAuth;
        m_services = services;
        emit servicesChanged();
    }
    const QString address = s.value(QStringLiteral("connectedAddress")).toString();
    if (m_connectedAddress != address) {
        m_connectedAddress = address;
        emit connectedAddressChanged();
    }
    const QString deviceName = s.value(QStringLiteral("connectedDeviceName")).toString();
    const int heartRate = s.value(QStringLiteral("heartRate"), -1).toInt();
    const QVariantMap bandInfo = s.value(QStringLiteral("bandInfo")).toMap();
    if (m_connectedDeviceName != deviceName || m_heartRate != heartRate
            || m_bandInfo != bandInfo) {
        m_connectedDeviceName = deviceName;
        m_heartRate = heartRate;
        m_bandInfo = bandInfo;
        emit bandInfoChanged();
    }
    const bool sleep = s.value(QStringLiteral("supportsSleep"), true).toBool();
    const bool stress = s.value(QStringLiteral("supportsStress"), true).toBool();
    const bool spo2 = s.value(QStringLiteral("supportsSpO2"), true).toBool();
    const bool estActivity = s.value(QStringLiteral("estimatedActivity")).toBool();
    if (m_supportsSleep != sleep || m_supportsStress != stress
            || m_supportsSpO2 != spo2 || m_estimatedActivity != estActivity) {
        m_supportsSleep = sleep;
        m_supportsStress = stress;
        m_supportsSpO2 = spo2;
        m_estimatedActivity = estActivity;
        emit capabilitiesChanged();
    }
    const QVariantList devices = s.value(QStringLiteral("devices")).toList();
    if (m_devices != devices) {
        m_devices = devices;
        emit devicesChanged();
    }
    const QVariantList results = s.value(QStringLiteral("activityResults")).toList();
    if (m_activityResults.size() != results.size()) {
        m_activityResults = results;
        emit activityResultsChanged();
    } else {
        m_activityResults = results;
    }
    // Сначала обновляем свойства прокси: обработчики dataChanged в QML
    // строят карточки в том числе из bandInfo и capabilities.
    const qulonglong revision = s.value(QStringLiteral("dataRevision")).toULongLong();
    if (!m_haveDataRevision || revision != m_dataRevision) {
        m_haveDataRevision = true;
        m_dataRevision = revision;
        if (m_storage)
            m_storage->refresh();
    }
}

// --- BLE-действия: транзитом в демон ---

void BandProxy::startScan() { callDaemon(QStringLiteral("startScan")); }
void BandProxy::stopScan() { callDaemon(QStringLiteral("stopScan")); }
void BandProxy::connectToBand(const QString &address)
{
    callDaemon(QStringLiteral("connectToBand"), address);
}
void BandProxy::disconnectBand() { callDaemon(QStringLiteral("disconnectBand")); }
void BandProxy::startBandAuth() { callDaemon(QStringLiteral("startBandAuth")); }
void BandProxy::syncActivity() { callDaemon(QStringLiteral("syncActivity")); }

void BandProxy::setAuthKey(const QString &hexKey)
{
    QString hex = hexKey.trimmed();
    if (hex.startsWith(QStringLiteral("0x")))
        hex = hex.mid(2);
    appSettings().setValue(QStringLiteral("miband8/authKey"), hex);
    callDaemon(QStringLiteral("setAuthKey"), hex);
}

void BandProxy::sendTestNotification(const QString &title, const QString &body,
                                     const QString &appName, const QString &package)
{
    callDaemon(QStringLiteral("sendTestNotification"), title, body, appName, package);
}

// --- Локальные настройки (appSettings) ---

void BandProxy::setStepsGoal(int goal)
{
    if (goal <= 0 || goal == m_stepsGoal)
        return;
    m_stepsGoal = goal;
    appSettings().setValue(QStringLiteral("stepsGoal"), goal);
    emit stepsGoalChanged();
}

void BandProxy::setCaloriesGoal(int goal)
{
    if (goal <= 0 || goal == m_caloriesGoal)
        return;
    m_caloriesGoal = goal;
    appSettings().setValue(QStringLiteral("ui/caloriesGoal"), goal);
    emit caloriesGoalChanged();
}

void BandProxy::setActivityGoal(int goal)
{
    if (goal <= 0 || goal == m_activityGoal)
        return;
    m_activityGoal = goal;
    appSettings().setValue(QStringLiteral("ui/activityGoal"), goal);
    emit activityGoalChanged();
}

double BandProxy::weightKg() const
{
    return appSettings().value(QStringLiteral("profile/weightKg"), 70.0).toDouble();
}

int BandProxy::heightCm() const
{
    return appSettings().value(QStringLiteral("profile/heightCm"), 170).toInt();
}

void BandProxy::setWeightKg(double value)
{
    if (!std::isfinite(value) || value < 20 || value > 300 || value == weightKg())
        return;
    appSettings().setValue(QStringLiteral("profile/weightKg"), value);
    if (m_storage)
        m_storage->recalculateCalories();
    emit profileChanged();
}

void BandProxy::setHeightCm(int value)
{
    if (value < 80 || value > 250 || value == heightCm())
        return;
    appSettings().setValue(QStringLiteral("profile/heightCm"), value);
    if (m_storage)
        m_storage->recalculateCalories();
    emit profileChanged();
}

void BandProxy::setLocalStatus(const QString &text)
{
    m_status = text;
    m_userStatus = text;
    emit statusChanged();
    emit userStatusChanged();
}

void BandProxy::tryStartDaemon()
{
    const QString name = QString::fromLatin1(DAEMON_UNIT);
    const QString src = QStringLiteral("/usr/share/ru.nighteugene.aurorafitness/") + name;
    const QString dirPath = QDir::homePath() + QStringLiteral("/.config/systemd/user");
    const QString dst = dirPath + QLatin1Char('/') + name;

    if (!QFile::exists(dst)) {
        QDir().mkpath(dirPath);
        if (!QFile::copy(src, dst)) {
            // Песочница GUI: запись в ~/.config/systemd запрещена
            setLocalStatus(tr("Служба браслета не установлена (песочница): выполните в терминале: cp %1 %2 && systemctl --user enable --now %3")
                           .arg(src, dst, name));
            return;
        }
        QProcess::execute(QStringLiteral("systemctl"),
                          {QStringLiteral("--user"), QStringLiteral("daemon-reload")});
    }
    QProcess::startDetached(QStringLiteral("systemctl"),
                            {QStringLiteral("--user"), QStringLiteral("start"), name});
}

void BandProxy::setDaemonEnabled(bool enabled)
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("daemon/notifyEnabled"), enabled);

    settings.sync();
    // Служба обслуживает GUI/BLE независимо от пересылки уведомлений.
    // Не выключаем её и не перезаписываем юнит из песочницы.
    callDaemon(QStringLiteral("getState")); // invoked -> reloadSettings демона
    setLocalStatus(enabled ? tr("Пересылка уведомлений включена")
                           : tr("Пересылка уведомлений выключена"));
}

bool BandProxy::daemonEnabled() const
{
    const QSettings settings = appSettings();
    return settings.value(QStringLiteral("daemon/notifyEnabled"), false).toBool();
}

void BandProxy::setDaemonSyncEnabled(bool enabled)
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("daemon/syncEnabled"), enabled);
    if (enabled && settings.value(QStringLiteral("daemon/syncIntervalMin"), 0).toInt() <= 0)
        settings.setValue(QStringLiteral("daemon/syncIntervalMin"), 30);
    settings.sync();
    callDaemon(QStringLiteral("getState"));
}

bool BandProxy::daemonSyncEnabled() const
{
    const QSettings settings = appSettings();
    return settings.value(QStringLiteral("daemon/syncEnabled"), false).toBool();
}

QString BandProxy::lastSyncTimeText() const
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

QVariantMap BandProxy::cardVisibility() const
{
    QVariantMap out;
    const QSettings settings = appSettings();
    for (const QString &id : cardIds())
        out.insert(id, settings.value(QStringLiteral("view/card/") + id, true).toBool());
    return out;
}

void BandProxy::setCardVisible(const QString &id, bool visible)
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("view/card/") + id, visible);
    emit viewConfigChanged();
}

QStringList BandProxy::cardOrder() const
{
    const QString saved = appSettings().value(QStringLiteral("view/order")).toString();
    QStringList order = saved.split(QLatin1Char(','), QString::SkipEmptyParts);
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

void BandProxy::moveCard(const QString &id, int dir)
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

void BandProxy::setCardOrder(const QStringList &order)
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
