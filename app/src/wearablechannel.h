#ifndef WEARABLECHANNEL_H
#define WEARABLECHANNEL_H

#include <QObject>
#include <QVariantMap>
#include <QVariantList>

class WearableChannel : public QObject
{
    Q_OBJECT
public:
    explicit WearableChannel(QObject *parent = nullptr) : QObject(parent) {}
    virtual bool requiresAuth() const = 0;
    virtual bool ready() const = 0;
    virtual QVariantMap capabilities() const = 0;
    virtual QString storageId(const QString &) const { return QString(); }
    virtual void start() = 0;
    virtual void setAuthKey(const QByteArray &) {}
    virtual void startAuth() {}
    virtual void sync() = 0;
    virtual void sendNotification(const QString &app, const QString &title, const QString &body,
                                  const QString &package = QString()) = 0;
    virtual void onCharacteristicValue(const QString &path, const QByteArray &value) = 0;
signals:
    void readyChanged();
    void authStatusChanged(const QString &status);
    void authFailed(const QString &reason);
    void batteryReceived(int level, int state);
    void deviceInfoReceived(const QString &serial, const QString &firmware, const QString &model);
    void activityFileParsed(const QVariantMap &data);
    void activityFetchProgress(const QString &status);
    void activityFetchFinished();
    void stepsReceived(quint32 steps);
    void heartRateReceived(int bpm);
    void error(const QString &message);
};

WearableChannel *createWearableChannel(const QVariantList &services, QObject *parent);

#endif
