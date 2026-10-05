// SPDX-License-Identifier: BSD-3-Clause

#ifndef BANDSERVICE_H
#define BANDSERVICE_H

#include <QObject>
#include <QVariantMap>

class BluezManager;

// D-Bus API демона: снапшот состояния браслета + команды от GUI.
// Живёт только в режиме --daemon. Сервис ru.nighteugene.aurorafitness.band
// (оно же — арбитр владения BLE-линком), путь /band, интерфейс
// ru.nighteugene.aurorafitness.band. Каждый входящий вызов логируется:
// песоченый GUI в журнал не пишет, отладка — только со стороны демона.
class BandService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "ru.nighteugene.aurorafitness.band")
public:
    explicit BandService(BluezManager *bluez, QObject *parent = nullptr);
    bool start();

public slots:
    QVariantMap getState();
    void startScan();
    void stopScan();
    void connectToBand(const QString &address);
    void disconnectBand();
    void syncActivity();
    void startBandAuth();
    void setAuthKey(const QString &hexKey);
    void sendTestNotification(const QString &title, const QString &body,
                              const QString &appName, const QString &package);

signals:
    void stateChanged(const QVariantMap &state);
    void activitySyncStarted();
    void activitySyncFinished();
    void deviceError(const QString &message);
    // Любой входящий вызов — демон по нему перечитывает настройки
    void invoked();
    // Смена ключа авторизации — демон по нему переподключается к браслету
    void authKeyChanged();

private slots:
    void scheduleStateEmit();
    void emitState();

private:
    QVariantMap collectState() const;

    BluezManager *m_bluez;
    bool m_emitScheduled = false;
};

#endif // BANDSERVICE_H
