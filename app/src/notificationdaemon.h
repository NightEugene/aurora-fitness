// SPDX-License-Identifier: BSD-3-Clause

#ifndef NOTIFICATIONDAEMON_H
#define NOTIFICATIONDAEMON_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QTimer>
#include <QQueue>
#include <QElapsedTimer>
#include <QDateTime>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;

class BluezManager;
class QSocketNotifier;

// Фоновый демон: единственный владелец браслета. Пересылка системных
// уведомлений на браслет + автосинк по таймеру.
// Перехват org.freedesktop.Notifications.Notify — через libdbus-1 (eavesdrop),
// т.к. Qt 5.6 eavesdrop на сессионной шине не поддерживает.
//
// GUI с браслетом напрямую не работает — ходит по D-Bus в BandService.
// Имя браслета отбирают только CLI-режимы (--notify и т.п.): по NameLost
// демон отпускает BLE-линк и встаёт в очередь за именем, по NameAcquired —
// подключается обратно.
class NotificationDaemon : public QObject
{
    Q_OBJECT
public:
    explicit NotificationDaemon(BluezManager *bluez, const QString &mac,
                                QObject *parent = nullptr);
    ~NotificationDaemon();

    bool start();

public slots:
    void reloadSettings();      // перечитать conf (зовётся и по D-Bus вызовам BandService)
    void ensureBandConnected(); // зовётся и при смене auth key из GUI

private slots:
    void onDbusReadyRead();
    void onMinuteTick();        // перечитать настройки + проверить автосинк
    void onBandReady();
    void onDeviceError(const QString &message);
    void onBandDisconnected();

private:
    void handleMessage(DBusMessage *msg);
    void handleNameSignal(DBusMessage *msg);
    void handleNotify(const QString &appName, const QString &summary, const QString &body,
                      const QString &package);
    // id приложения-источника: hint x-aurora-application-id или
    // поиск по имени в /usr/share/applications/*.desktop
    QString resolveAppPackage(const QString &appName, const QString &hintId) const;
    // Кэширует иконку пакета в общий конфиг-каталог
    void cacheIcon(const QString &package) const;
    void flushPendingNotification();
    void requestSync();
    // Имя браслета — через Qt-соединение (там живёт D-Bus объект /band);
    // возвращает DBUS_REQUEST_NAME_REPLY_* или -1 при ошибке
    int requestBandNameQt();
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
    QDateTime m_lastAttempt;
    QTimer m_notificationTimer;

    bool m_connecting = false;
    bool m_bandAllowed = false; // владеем ли D-Bus-именем браслета (CLI отбирает)
    struct PendingNotification {
        QString app, title, body, package, address;
        QDateTime created;
    };
    QQueue<PendingNotification> m_notifications;
    bool m_syncPending = false;
};

#endif // NOTIFICATIONDAEMON_H
