// SPDX-License-Identifier: BSD-3-Clause

#include "bandservice.h"
#include "bluezmanager.h"
#include "devicesmodel.h"

#include <QDBusConnection>
#include <QTimer>
#include <QDebug>

namespace {
const char BAND_PATH[] = "/band";
}

BandService::BandService(BluezManager *bluez, QObject *parent)
    : QObject(parent), m_bluez(bluez)
{
    // Любое изменение состояния браслета — отложенная (со схлопыванием)
    // рассылка снапшота stateChanged
    connect(m_bluez, &BluezManager::scanningChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::adapterPoweredChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::statusChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::userStatusChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::servicesChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::bandInfoChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::connectedAddressChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::authStatusChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::busyChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::bandReadyChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::capabilitiesChanged, this, &BandService::scheduleStateEmit);
    connect(m_bluez, &BluezManager::activityResultsChanged, this, &BandService::scheduleStateEmit);

    DevicesModel *devices = m_bluez->devices();
    connect(devices, &QAbstractListModel::rowsInserted, this, &BandService::scheduleStateEmit);
    connect(devices, &QAbstractListModel::rowsRemoved, this, &BandService::scheduleStateEmit);
    connect(devices, &QAbstractListModel::modelReset, this, &BandService::scheduleStateEmit);
    connect(devices, &QAbstractListModel::dataChanged, this, &BandService::scheduleStateEmit);

    // Отдельные сигналы для GUI-индикатора синка и ошибок
    connect(m_bluez, &BluezManager::activitySyncStarted, this, [this]() {
        emit activitySyncStarted();
        scheduleStateEmit();
    });
    connect(m_bluez, &BluezManager::activitySyncFinished, this, [this]() {
        emit activitySyncFinished();
        scheduleStateEmit();
    });
    connect(m_bluez, &BluezManager::deviceError, this, [this](const QString &message) {
        emit deviceError(message);
        scheduleStateEmit();
    });
}

bool BandService::start()
{
    if (!QDBusConnection::sessionBus().registerObject(
                QString::fromLatin1(BAND_PATH), this,
                QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qWarning() << "[band-api] registerObject(" << BAND_PATH << ") failed";
        return false;
    }
    qInfo() << "[band-api] D-Bus API поднят на" << BAND_PATH;
    return true;
}

QVariantMap BandService::collectState() const
{
    QVariantMap s;
    s.insert(QStringLiteral("scanning"), m_bluez->scanning());
    s.insert(QStringLiteral("adapterPowered"), m_bluez->adapterPowered());
    s.insert(QStringLiteral("status"), m_bluez->status());
    s.insert(QStringLiteral("userStatus"), m_bluez->userStatus());
    s.insert(QStringLiteral("authStatus"), m_bluez->authStatus());
    s.insert(QStringLiteral("busy"), m_bluez->busy());
    s.insert(QStringLiteral("ready"), m_bluez->bandReady());
    s.insert(QStringLiteral("requiresAuth"), m_bluez->requiresAuth());
    s.insert(QStringLiteral("connectedAddress"), m_bluez->connectedAddress());
    s.insert(QStringLiteral("connectedDeviceName"), m_bluez->connectedDeviceName());
    s.insert(QStringLiteral("heartRate"), m_bluez->heartRate());
    s.insert(QStringLiteral("supportsSleep"), m_bluez->supportsSleep());
    s.insert(QStringLiteral("supportsStress"), m_bluez->supportsStress());
    s.insert(QStringLiteral("supportsSpO2"), m_bluez->supportsSpO2());
    s.insert(QStringLiteral("estimatedActivity"), m_bluez->estimatedActivity());
    s.insert(QStringLiteral("bandInfo"), m_bluez->bandInfo());
    s.insert(QStringLiteral("services"), m_bluez->services());
    s.insert(QStringLiteral("devices"), m_bluez->devices()->toList());

    // Массивы сэмплов по шине не гоняем (тысячи записей на каждый апдейт) —
    // GUI нужна только длина, подменяем списком нулей того же размера
    // (QVariant() недопустим: D-Bus не маршалит invalid-варианты)
    QVariantList results;
    for (const QVariant &v : m_bluez->activityResults()) {
        QVariantMap m = v.toMap();
        for (const QString &k : {QStringLiteral("samples"), QStringLiteral("stages")}) {
            const int n = m.value(k).toList().size();
            if (n > 0) {
                QVariantList stub;
                stub.reserve(n);
                for (int i = 0; i < n; ++i)
                    stub.append(QVariant(0));
                m.insert(k, stub);
            }
        }
        results.append(m);
    }
    s.insert(QStringLiteral("activityResults"), results);
    return s;
}

QVariantMap BandService::getState()
{
    qInfo() << "[band-api] вызов getState";
    emit invoked();
    return collectState();
}

void BandService::startScan()
{
    qInfo() << "[band-api] вызов startScan";
    emit invoked();
    m_bluez->startScan();
}

void BandService::stopScan()
{
    qInfo() << "[band-api] вызов stopScan";
    emit invoked();
    m_bluez->stopScan();
}

void BandService::connectToBand(const QString &address)
{
    qInfo() << "[band-api] вызов connectToBand" << address;
    emit invoked();
    m_bluez->connectToBand(address);
}

void BandService::disconnectBand()
{
    qInfo() << "[band-api] вызов disconnectBand";
    emit invoked();
    m_bluez->disconnectBand();
}

void BandService::syncActivity()
{
    qInfo() << "[band-api] вызов syncActivity";
    emit invoked();
    m_bluez->syncActivity();
}

void BandService::startBandAuth()
{
    qInfo() << "[band-api] вызов startBandAuth";
    emit invoked();
    m_bluez->startBandAuth();
}

void BandService::setAuthKey(const QString &hexKey)
{
    qInfo() << "[band-api] вызов setAuthKey (длина" << hexKey.size() << ")";
    emit invoked();
    m_bluez->setAuthKey(hexKey);
    emit authKeyChanged();
}

void BandService::sendTestNotification(const QString &title, const QString &body,
                                       const QString &appName, const QString &package)
{
    qInfo() << "[band-api] вызов sendTestNotification" << appName << "—" << title;
    emit invoked();
    m_bluez->sendTestNotification(title, body, appName, package);
}

void BandService::scheduleStateEmit()
{
    // Схлопывание: во время синка statusChanged идёт пачками
    if (m_emitScheduled)
        return;
    m_emitScheduled = true;
    QTimer::singleShot(200, this, &BandService::emitState);
}

void BandService::emitState()
{
    m_emitScheduled = false;
    emit stateChanged(collectState());
}
