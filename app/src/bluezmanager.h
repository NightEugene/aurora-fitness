// SPDX-License-Identifier: BSD-3-Clause

#ifndef BLUEZMANAGER_H
#define BLUEZMANAGER_H

#include <QObject>
#include <QMap>
#include <QVariantMap>
#include <QDBusObjectPath>
#include <QTimer>

#include "devicesmodel.h"
#include "storage.h"

class XiaomiChannel;

typedef QMap<QString, QVariantMap> InterfaceMap;
typedef QMap<QDBusObjectPath, InterfaceMap> ManagedObjectMap;

Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjectMap)

class QDBusMessage;

// GATT-клиент поверх BlueZ через системную шину D-Bus (org.bluez).
class BluezManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool adapterPowered READ adapterPowered NOTIFY adapterPoweredChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString userStatus READ userStatus NOTIFY userStatusChanged)
    Q_PROPERTY(DevicesModel* devices READ devices CONSTANT)
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(QVariantMap bandInfo READ bandInfo NOTIFY bandInfoChanged)
    Q_PROPERTY(QString connectedAddress READ connectedAddress NOTIFY connectedAddressChanged)
    Q_PROPERTY(QString connectedDeviceName READ connectedDeviceName NOTIFY bandInfoChanged)
    Q_PROPERTY(int stepsGoal READ stepsGoal WRITE setStepsGoal NOTIFY stepsGoalChanged)
    Q_PROPERTY(int caloriesGoal READ caloriesGoal WRITE setCaloriesGoal NOTIFY caloriesGoalChanged)
    Q_PROPERTY(int activityGoal READ activityGoal WRITE setActivityGoal NOTIFY activityGoalChanged)
    Q_PROPERTY(QString authStatus READ authStatus NOTIFY authStatusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList activityResults READ activityResults NOTIFY activityResultsChanged)

public:
    explicit BluezManager(QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    bool adapterPowered() const { return m_adapterPowered; }
    QString status() const { return m_status; }
    // Только осмысленные для пользователя статусы (без служебных «Адаптер готов» и т.п.)
    QString userStatus() const { return m_userStatus; }
    DevicesModel *devices() { return &m_devices; }
    QVariantList services() const { return m_services; }
    QVariantMap bandInfo() const { return m_bandInfo; }
    QString connectedAddress() const { return m_connectedAddress; }
    // Имя подключённого устройства: bandInfo → Alias/Name из BlueZ → MAC
    QString connectedDeviceName() const;
    int stepsGoal() const { return m_stepsGoal; }
    void setStepsGoal(int goal);
    int caloriesGoal() const { return m_caloriesGoal; }
    void setCaloriesGoal(int goal);
    int activityGoal() const { return m_activityGoal; }
    void setActivityGoal(int goal);
    QString authStatus() const { return m_authStatus; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void connectToBand(const QString &address);
    // Подключиться, дождавшись пока прежний владелец (демон) отпустит линк
    Q_INVOKABLE void connectToBandWhenFree(const QString &address, int attempt = 0);
    Q_INVOKABLE bool bandLinkActive(const QString &address) const;
    Q_INVOKABLE void disconnectBand();
    Q_INVOKABLE void setAuthKey(const QString &hexKey);
    Q_INVOKABLE void startBandAuth();
    Q_INVOKABLE void syncActivity();
    Q_INVOKABLE void sendTestNotification(const QString &title, const QString &body,
                                          const QString &appName = QString());

    // --- поддержка фонового демона (--daemon) ---
    Q_INVOKABLE bool bandReady() const;     // подключён и авторизован
    Q_INVOKABLE bool powerOnAdapter();      // включить BT через Properties.Set
    Q_INVOKABLE void setDaemonEnabled(bool enabled);
    Q_INVOKABLE bool daemonEnabled() const;
    Q_INVOKABLE void setDaemonSyncEnabled(bool enabled);
    Q_INVOKABLE bool daemonSyncEnabled() const;

    // --- GUI ---
    // Автоподключение к последнему браслету (miband8/lastAddress) при старте GUI;
    // после первой успешной auth — автоматический syncActivity()
    Q_INVOKABLE void autoConnectLast();
    // Время последнего синка из QSettings для строки состояния («HH:mm» / «dd.MM HH:mm»)
    Q_INVOKABLE QString lastSyncTimeText() const;

    QVariantList activityResults() const { return m_activityResults; }
    Storage *storage() { return &m_storage; }

public slots:
    // Консольный режим: печатает результат в stdout и завершает приложение.
    void cliScanFinished();
    void cliReadFinished();

signals:
    void scanningChanged();
    void adapterPoweredChanged();
    void statusChanged();
    void userStatusChanged();
    void servicesChanged();
    void bandInfoChanged();
    void connectedAddressChanged();
    void stepsGoalChanged();
    void caloriesGoalChanged();
    void activityGoalChanged();
    void authStatusChanged();
    void busyChanged();
    void deviceReady();   // подключились и прочитали сервисы/характеристики
    void deviceError(const QString &message);
    void bandBatteryReceived(int level, int state);
    void activityResultsChanged();
    void activitySyncStarted();
    void activitySyncFinished();
    void bandDisconnected();   // обрыв соединения с браслетом

private slots:
    void onInterfacesAdded(const QDBusObjectPath &path, const InterfaceMap &interfaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void onPropertiesChanged(const QString &interface, const QVariantMap &props,
                             const QStringList &invalidated, const QDBusMessage &message);
    void onResolveTimeout();

private:
    bool initAdapter();
    bool deviceKnown(const QString &path) const;
    void doConnect();
    ManagedObjectMap managedObjects() const;
    void subscribeSignals();
    void updateFromManagedObjects();
    void finishConnect();
    void enumerateServices();
    void readBandInfo();
    void setupXiaomiChannel();
    QString readStringChar(const QString &charPath);
    void setStatus(const QString &status);
    void setBusy(bool busy);
    void notifyGoalsAchieved();
    void sendSystemNotification(const QString &summary, const QString &body);

    static QString devicePathForAddress(const QString &address);

    QString m_adapterPath;
    bool m_adapterPowered = false;
    bool m_scanning = false;
    bool m_signalsSubscribed = false;
    QString m_status;
    QString m_userStatus;
    DevicesModel m_devices;

    QString m_pendingPath;      // объектный путь устройства, к которому подключаемся
    bool m_busy = false;        // идёт подключение/auth — для индикатора в UI
    bool m_waitingForDevice = false; // ждём появления устройства в сканировании
    QString m_connectedAddress;
    int m_stepsGoal = 10000;
    int m_caloriesGoal = 500;
    int m_activityGoal = 30;
    QVariantList m_services;
    QVariantMap m_bandInfo;
    QTimer m_resolveTimer;
    int m_resolveAttempts = 0;
    bool m_cliMode = false;

    XiaomiChannel *m_channel = nullptr;
    QString m_authKeyHex;
    QString m_authStatus;
    QVariantList m_activityResults;
    Storage m_storage;
};

#endif // BLUEZMANAGER_H
