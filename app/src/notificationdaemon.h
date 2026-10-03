// SPDX-License-Identifier: BSD-3-Clause

#ifndef NOTIFICATIONDAEMON_H
#define NOTIFICATIONDAEMON_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include <QDateTime>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;

class BluezManager;
class QSocketNotifier;

// Фоновый демон: пересылка системных уведомлений на браслет + автосинк по таймеру.
// Перехват org.freedesktop.Notifications.Notify — через libdbus-1 (eavesdrop),
// т.к. Qt 5.6 eavesdrop на сессионной шине не поддерживает.
class NotificationDaemon : public QObject
{
    Q_OBJECT
public:
    explicit NotificationDaemon(BluezManager *bluez, const QString &mac,
                                QObject *parent = nullptr);
    ~NotificationDaemon();

    bool start();

private slots:
    void onDbusReadyRead();
    void onMinuteTick();        // перечитать настройки + проверить автосинк
    void onBandReady();
    void onDeviceError(const QString &message);
    void onBandDisconnected();

private:
    void handleMessage(DBusMessage *msg);
    void handleNameSignal(DBusMessage *msg);
    void handleNotify(const QString &appName, const QString &summary, const QString &body);
    void flushPendingNotification();
    void ensureBandConnected();
    void requestSync();
    void reloadSettings();
    QMap<QString, QString> readConfFile() const;

    BluezManager *m_bluez;
    DBusConnection *m_conn = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QString m_mac;

    QTimer m_minuteTimer;
    bool m_notifyEnabled = false;
    bool m_syncEnabled = false;
    int m_syncIntervalMin = 30;
    QDateTime m_lastSync;

    bool m_connecting = false;
    bool m_bandAllowed = false; // владеем ли D-Bus-именем браслета (GUI отбирает)
    bool m_pendingNotification = false;
    QString m_pendingApp;
    QString m_pendingTitle;
    QString m_pendingBody;
    bool m_syncPending = false;
};

#endif // NOTIFICATIONDAEMON_H
