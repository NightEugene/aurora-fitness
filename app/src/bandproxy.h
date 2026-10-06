// SPDX-License-Identifier: BSD-3-Clause

#ifndef BANDPROXY_H
#define BANDPROXY_H

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

class Storage;
class QDBusServiceWatcher;
class QDBusPendingCallWatcher;

// Клиентская обёртка над D-Bus API демона (BandService). Для QML выглядит
// как BluezManager (context property "bluez" — те же имена property/методов
// и сигналов), но BLE не трогает: демон — единственный владелец браслета.
// Цели/профиль/вид/переключатели демона хранятся в appSettings() и
// обрабатываются локально; состояние браслета и BLE-действия — по D-Bus.
class BandProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool adapterPowered READ adapterPowered NOTIFY adapterPoweredChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString userStatus READ userStatus NOTIFY userStatusChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(QVariantMap bandInfo READ bandInfo NOTIFY bandInfoChanged)
    Q_PROPERTY(QString connectedAddress READ connectedAddress NOTIFY connectedAddressChanged)
    Q_PROPERTY(QString connectedDeviceName READ connectedDeviceName NOTIFY bandInfoChanged)
    Q_PROPERTY(int stepsGoal READ stepsGoal WRITE setStepsGoal NOTIFY stepsGoalChanged)
    Q_PROPERTY(int caloriesGoal READ caloriesGoal WRITE setCaloriesGoal NOTIFY caloriesGoalChanged)
    Q_PROPERTY(int activityGoal READ activityGoal WRITE setActivityGoal NOTIFY activityGoalChanged)
    Q_PROPERTY(QString authStatus READ authStatus NOTIFY authStatusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool requiresAuth READ requiresAuth NOTIFY servicesChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY bandReadyChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY bandInfoChanged)
    Q_PROPERTY(bool supportsSleep READ supportsSleep NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsStress READ supportsStress NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool supportsSpO2 READ supportsSpO2 NOTIFY capabilitiesChanged)
    Q_PROPERTY(bool estimatedActivity READ estimatedActivity NOTIFY capabilitiesChanged)
    Q_PROPERTY(double weightKg READ weightKg WRITE setWeightKg NOTIFY profileChanged)
    Q_PROPERTY(int heightCm READ heightCm WRITE setHeightCm NOTIFY profileChanged)
    Q_PROPERTY(QVariantList activityResults READ activityResults NOTIFY activityResultsChanged)
    Q_PROPERTY(QVariantMap cardVisibility READ cardVisibility NOTIFY viewConfigChanged)
    Q_PROPERTY(QStringList cardOrder READ cardOrder NOTIFY viewConfigChanged)

public:
    explicit BandProxy(Storage *storage, QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    bool adapterPowered() const { return m_adapterPowered; }
    QString status() const { return m_status; }
    QString userStatus() const { return m_userStatus; }
    QVariantList devices() const { return m_devices; }
    QVariantList services() const { return m_services; }
    QVariantMap bandInfo() const { return m_bandInfo; }
    QString connectedAddress() const { return m_connectedAddress; }
    QString connectedDeviceName() const { return m_connectedDeviceName; }
    int stepsGoal() const { return m_stepsGoal; }
    void setStepsGoal(int goal);
    int caloriesGoal() const { return m_caloriesGoal; }
    void setCaloriesGoal(int goal);
    int activityGoal() const { return m_activityGoal; }
    void setActivityGoal(int goal);
    QString authStatus() const { return m_authStatus; }
    bool busy() const { return m_busy; }
    bool requiresAuth() const { return m_requiresAuth; }
    bool ready() const { return m_ready; }
    int heartRate() const { return m_heartRate; }
    bool supportsSleep() const { return m_supportsSleep; }
    bool supportsStress() const { return m_supportsStress; }
    bool supportsSpO2() const { return m_supportsSpO2; }
    bool estimatedActivity() const { return m_estimatedActivity; }
    double weightKg() const;
    int heightCm() const;
    void setWeightKg(double value);
    void setHeightCm(int value);
    QVariantList activityResults() const { return m_activityResults; }
    QVariantMap cardVisibility() const;
    QStringList cardOrder() const;

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void connectToBand(const QString &address);
    Q_INVOKABLE void disconnectBand();
    Q_INVOKABLE void setAuthKey(const QString &hexKey);
    Q_INVOKABLE void startBandAuth();
    Q_INVOKABLE void syncActivity();
    Q_INVOKABLE void sendTestNotification(const QString &title, const QString &body,
                                          const QString &appName = QString(),
                                          const QString &package = QString());
    Q_INVOKABLE void setDaemonEnabled(bool enabled);
    Q_INVOKABLE bool daemonEnabled() const;
    Q_INVOKABLE void setDaemonSyncEnabled(bool enabled);
    Q_INVOKABLE bool daemonSyncEnabled() const;
    Q_INVOKABLE QString lastSyncTimeText() const;
    Q_INVOKABLE void setCardVisible(const QString &id, bool visible);
    Q_INVOKABLE void moveCard(const QString &id, int dir);
    Q_INVOKABLE void setCardOrder(const QStringList &order);

signals:
    void scanningChanged();
    void adapterPoweredChanged();
    void statusChanged();
    void userStatusChanged();
    void devicesChanged();
    void servicesChanged();
    void bandInfoChanged();
    void connectedAddressChanged();
    void stepsGoalChanged();
    void caloriesGoalChanged();
    void activityGoalChanged();
    void authStatusChanged();
    void busyChanged();
    void bandReadyChanged();
    void capabilitiesChanged();
    void profileChanged();
    void viewConfigChanged();
    void activityResultsChanged();
    void activitySyncStarted();
    void activitySyncFinished();
    void deviceError(const QString &message);

private slots:
    void onStateChanged(const QVariantMap &state);
    void onGetStateFinished(QDBusPendingCallWatcher *watcher);
    void onServiceRegistered();
    void onServiceUnregistered();

private:
    void callDaemon(const QString &method,
                    const QVariant &a1 = QVariant(), const QVariant &a2 = QVariant(),
                    const QVariant &a3 = QVariant(), const QVariant &a4 = QVariant());
    void refreshState();
    void setOffline(const QString &reason);
    // Попытка запустить службу демона (копирование юнита + systemctl --user start)
    void tryStartDaemon();
    // Локальное сообщение вместо статуса демона (например, отказ песочницы)
    void setLocalStatus(const QString &text);

    Storage *m_storage;
    qulonglong m_dataRevision = 0;
    bool m_haveDataRevision = false;
    QDBusServiceWatcher *m_watcher = nullptr;

    // Зеркало состояния демона
    bool m_scanning = false;
    bool m_adapterPowered = false;
    QString m_status;
    QString m_userStatus;
    QVariantList m_devices;
    QVariantList m_services;
    QVariantMap m_bandInfo;
    QString m_connectedAddress;
    QString m_connectedDeviceName;
    QString m_authStatus;
    bool m_busy = false;
    bool m_requiresAuth = false;
    bool m_ready = false;
    int m_heartRate = -1;
    bool m_supportsSleep = true;
    bool m_supportsStress = true;
    bool m_supportsSpO2 = true;
    bool m_estimatedActivity = false;
    QVariantList m_activityResults;

    // Локальные настройки (appSettings)
    int m_stepsGoal = 10000;
    int m_caloriesGoal = 500;
    int m_activityGoal = 30;
};

#endif // BANDPROXY_H
