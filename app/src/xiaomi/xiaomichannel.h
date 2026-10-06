// SPDX-License-Identifier: BSD-3-Clause

#ifndef XIAOMI_CHANNEL_H
#define XIAOMI_CHANNEL_H

#include "../wearablechannel.h"
#include <QByteArray>
#include <QMap>
#include <QSet>

// Канал протокола Xiaomi (Mi Band 8): транспорт поверх GATT + auth-handshake.
// Реализация портирована из Gadgetbridge (XiaomiBleProtocolV1 / XiaomiAuthService).
class XiaomiChannel : public WearableChannel
{
    Q_OBJECT
public:
    explicit XiaomiChannel(QObject *parent = nullptr);

    void setup(const QString &readCharPath, const QString &writeCharPath,
               const QString &activityCharPath = QString(),
               const QString &dataUploadCharPath = QString());
    void setAuthKey(const QByteArray &key16);

    Q_INVOKABLE void startAuth();
    Q_INVOKABLE void startActivityFetch();

    // Health-команда (type=8) по командному каналу; healthProto может быть пустым
    void sendHealthCommand(quint32 subtype, const QByteArray &healthProto);

    // Уведомление на браслет (type=9 subtype=0, Notification3)
    void sendNotification(const QString &appName, const QString &title, const QString &body,
                          const QString &package = QString()) override;

    // Вызывается BluezManager'ом при изменении Value любой характеристики
    void onCharacteristicValue(const QString &path, const QByteArray &value);

    bool isAuthenticated() const { return m_authed; }
    bool ready() const override { return m_authed; }
    bool requiresAuth() const override { return true; }
    QVariantMap capabilities() const override {
        return {{QStringLiteral("sleep"), true}, {QStringLiteral("stress"), true},
                {QStringLiteral("spo2"), true}, {QStringLiteral("nativeCalories"), true},
                {QStringLiteral("nativeActivity"), true}};
    }
    void start() override {}
    void sync() override { startActivityFetch(); }

    // --- интерфейс для DataUpload (характеристика 0x0055, type=22) ---
    void sendDataUploadCommand(const QByteArray &dataUploadProto);
    // Шифрование для upload-характеристики: counter всегда 0 (без инкремента)
    QByteArray encryptUploadPayload(const QByteArray &plain) const;
    void writeUploadChar(const QByteArray &frame);
    int uploadWriteSize() const; // макс. размер записи (MTU-3)

signals:
    void authenticated();

private:
    enum class State { Idle, WaitWatchNonce, WaitConfirm, Authed };

    // --- транспорт ---
    void startNotify(const QString &charPath);
    // FIFO-очередь записей: не больше одной незавершённой WriteValue,
    // «In Progress» от BlueZ ретраится через 100 мс (до 10 раз)
    void writeValue(const QString &charPath, const QByteArray &value);
    void pumpWriteQueue();
    void sendAck(const QString &charPath);
    void sendPlainCommand(const QByteArray &proto);
    void sendEncryptedCommand(const QByteArray &proto);
    void handlePacket(const QString &charPath, const QByteArray &packet);
    // Доставка собранного payload: командный канал (0x51/0x52) или activity (0x53)
    void deliverPayload(const QString &charPath, bool encrypted, const QByteArray &payload);
    void processCommand(const QByteArray &payload);

    // --- auth ---
    void sendAppNonce();
    void handleWatchNonce(const QByteArray &authMsg);
    void sendAppConfirm();
    void deriveKeys();

    // --- команды после auth ---
    void sendCurrentTime();
    void requestBattery();
    void requestDeviceInfo();
    void handleSystem(const QByteArray &systemMsg);

public:
    // Пути-кандидаты иконки пакета (hicolor → тема по Icon= → маркеры
    // __system/__unknown). Пустой список/отсутствие файлов = иконки нет.
    // Используется и демоном для кэша.
    static QStringList iconCandidatePaths(const QString &pkg);

private:

    // --- уведомления / иконки ---
    void handleNotification(quint32 subtype, const QByteArray &notificationProto);
    void handleNotificationIconQuery(const QByteArray &iconPackageProto);
    void handleNotificationIconRequest(const QByteArray &iconRequestProto);
    // Первый существующий файл иконки пакета (кэш → iconCandidatePaths)
    QString findIconPath(const QString &pkg) const;
    QByteArray buildIconBitmap(quint32 pixelFormat, quint32 size) const;

    static QByteArray makeNonce(const QByteArray &nonce4, quint32 counter);

    QString m_readPath;
    QString m_writePath;
    QString m_activityPath;
    QString m_uploadPath;
    class ActivityFetcher *m_fetcher = nullptr;
    class DataUpload *m_uploader = nullptr;
    QString m_iconPackage;
    // Учёт загрузок по package:size; одновременно передаём одну иконку.
    QSet<QString> m_iconServed;
    QString m_iconUploading;
    int m_iconUploadingSize = 0;
    QByteArray m_authKey;
    State m_state = State::Idle;
    bool m_authed = false;

    QByteArray m_phoneNonce;   // 16 байт
    QByteArray m_watchNonce;   // 16 байт
    QByteArray m_encryptionKey;
    QByteArray m_decryptionKey;
    QByteArray m_encryptionNonce4;
    QByteArray m_decryptionNonce4;
    quint32 m_encCounter = 1;

    // Сборка входящих chunked-сообщений
    QMap<quint16, QByteArray> m_rxChunks;
    int m_rxExpectedChunks = 0;
    bool m_rxEncrypted = false;
    QString m_rxCharPath;
    quint32 m_notificationId = 1;

    // Очередь исходящих записей (BlueZ не терпит двух WriteValue параллельно)
    struct PendingWrite {
        QString path;
        QByteArray value;
        int retries = 0;
    };
    QList<PendingWrite> m_writeQueue;
    bool m_writeBusy = false;
    // Эхо-фильтр: этот BlueZ шлёт PropertiesChanged(Value) и на наши записи
    QList<QPair<QString, QByteArray>> m_ownWrites;
    // Дедуп двойной доставки сигнала (path+value в окне 200 мс)
    QString m_lastRxPath;
    QByteArray m_lastRxValue;
    qint64 m_lastRxMs = 0;
};

#endif // XIAOMI_CHANNEL_H
