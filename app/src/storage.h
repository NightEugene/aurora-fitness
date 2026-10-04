// SPDX-License-Identifier: BSD-3-Clause

#ifndef STORAGE_H
#define STORAGE_H

#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>

// Локальное хранилище активности (SQLite): дневные сводки, поминутные сэмплы,
// сессии сна и ручные измерения, распарсенные из файлов браслета.
class Storage : public QObject
{
    Q_OBJECT
public:
    explicit Storage(QObject *parent = nullptr);

    // Диспетчер по m["kind"]: dailySummary / dailyDetails / sleep / manualSamples
    void saveParsed(const QVariantMap &m);

    Q_INVOKABLE QVariantMap todaySummary();
    Q_INVOKABLE QVariantList dailySummaries(int days);
    Q_INVOKABLE QVariantList sleepSessions(int limit);
    Q_INVOKABLE QVariantList sleepStages(qlonglong bedTime);
    Q_INVOKABLE QVariantList minuteSamples(qint64 fromTs, qint64 toTs);
    Q_INVOKABLE int minuteSampleCount();

signals:
    void dataChanged();

private:
    bool open();
    void saveDailySummary(const QVariantMap &m);
    void saveDailyDetails(const QVariantMap &m);
    void saveSleep(const QVariantMap &m);
    void saveManualSamples(const QVariantMap &m);

    QSqlDatabase m_db;
    bool m_ready = false;
};

#endif // STORAGE_H
