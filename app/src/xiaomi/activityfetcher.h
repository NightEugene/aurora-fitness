// SPDX-License-Identifier: BSD-3-Clause

#ifndef XIAOMI_ACTIVITYFETCHER_H
#define XIAOMI_ACTIVITYFETCHER_H

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QTimer>

class XiaomiChannel;

// Выгрузка activity-файлов Mi Band 8 (XiaomiHealthService / XiaomiActivityFileFetcher
// из Gadgetbridge). Команды по командному каналу (type=8), файлы стримятся на 0x0053.
class ActivityFetcher : public QObject
{
    Q_OBJECT
public:
    explicit ActivityFetcher(XiaomiChannel *channel);

    void start();
    // Ответы браслета type=8: subtype + содержимое поля Health (Command field 10)
    void handleHealthResponse(quint32 subtype, const QByteArray &healthBytes);
    // Дешифрованная порция файла с характеристики 0x0053
    void addFilePortion(const QByteArray &portion);

    bool isRunning() const { return m_state != Idle && m_state != Done; }

signals:
    void fileParsed(const QVariantMap &data);
    void fetchProgress(const QString &status);
    void finished();

private:
    enum State { Idle, WaitTodayList, WaitPastList, Fetching, Done };

    void requestNextFile();
    void ackFile(const QByteArray &fileId);
    void onTimeout();
    void finish();
    static int fetchOrder(quint8 detailType);

    XiaomiChannel *m_channel;
    State m_state = Idle;
    QList<QByteArray> m_queue;      // fileId (7 байт), отсортированные
    QByteArray m_currentFileId;
    QByteArray m_fileBuffer;
    int m_portionsTotal = 0;
    int m_portionsReceived = 0;
    QTimer m_timeout;
};

#endif // XIAOMI_ACTIVITYFETCHER_H
