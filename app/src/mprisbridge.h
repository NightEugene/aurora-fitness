#ifndef MPRISBRIDGE_H
#define MPRISBRIDGE_H

#include <QObject>
#include <QVariantMap>
#include <QTimer>

class MprisBridge : public QObject
{
    Q_OBJECT
public:
    explicit MprisBridge(QObject *parent = nullptr, bool useRelay = true);
    void start();
    void refresh();
    void command(quint8 event);
    QVariantMap properties() const { return m_properties; }
signals:
    void propertiesChanged(const QVariantMap &properties);
private:
    void refreshPlayers();
    QTimer m_timer;
    QString m_service;
    QVariantMap m_properties;
    bool m_refreshing = false;
    bool m_useRelay;
    bool m_usingRelay = false;
};

class MprisRelay : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "ru.nighteugene.aurorafitness.media")
public:
    explicit MprisRelay(QObject *parent = nullptr);
    bool start();
public slots:
    QVariantMap getProperties() const;
    void control(int event);
private:
    MprisBridge m_bridge;
};

#endif
