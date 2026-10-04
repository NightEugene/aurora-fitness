#ifndef PINETIMECHANNEL_H
#define PINETIMECHANNEL_H

#include "wearablechannel.h"
#include <QVariantList>
#include <QMap>
#include <QQueue>
#include <QTimer>
#include <QElapsedTimer>

class MprisBridge;

class PineTimeChannel : public WearableChannel
{
    Q_OBJECT
public:
    explicit PineTimeChannel(QObject *parent = nullptr);
    void setup(const QVariantList &services);
    void start();
    bool ready() const override { return m_ready; }
    bool requiresAuth() const override { return false; }
    QVariantMap capabilities() const override {
        return {{QStringLiteral("sleep"), false}, {QStringLiteral("stress"), false},
                {QStringLiteral("spo2"), false}, {QStringLiteral("nativeCalories"), false},
                {QStringLiteral("nativeActivity"), false}};
    }
    QString storageId(const QString &address) const override { return address; }
    void sync();
    void sendNotification(const QString &app, const QString &title, const QString &body);
    void onCharacteristicValue(const QString &path, const QByteArray &value);
private:
    struct Operation {
        QString uuid;
        QString method;
        QByteArray data;
        int retries = 0;
    };
    void enqueue(const QString &uuid, const QString &method, const QByteArray &data = QByteArray());
    void pump();
    void setTime();
    void publishMusic(const QVariantMap &properties);
    QMap<QString, QString> m_paths;
    QMap<QString, QStringList> m_flags;
    QQueue<Operation> m_queue;
    bool m_running = false;
    bool m_ready = false;
    bool m_starting = false;
    bool m_syncing = false;
    bool m_syncFailed = false;
    QTimer m_poll;
    QElapsedTimer m_eventTime;
    QByteArray m_lastEvent;
    QMap<QString, QByteArray> m_musicValues;
    MprisBridge *m_mpris;
};

#endif
