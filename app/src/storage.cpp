// SPDX-License-Identifier: BSD-3-Clause

#include "storage.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include "appsettings.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVector>

// Отображение QVariant-ключей распарсенных данных на колонки БД
static const QMap<QString, QString> SUMMARY_COLUMNS = {
    {QStringLiteral("steps"), QStringLiteral("steps")},
    {QStringLiteral("calories"), QStringLiteral("calories")},
    {QStringLiteral("restingHr"), QStringLiteral("resting_hr")},
    {QStringLiteral("maxHr"), QStringLiteral("max_hr")},
    {QStringLiteral("minHr"), QStringLiteral("min_hr")},
    {QStringLiteral("avgHr"), QStringLiteral("avg_hr")},
    {QStringLiteral("stressAvg"), QStringLiteral("stress_avg")},
    {QStringLiteral("spo2Avg"), QStringLiteral("spo2_avg")},
    {QStringLiteral("activityMin"), QStringLiteral("activity_min")},
};

Storage::Storage(QObject *parent) : QObject(parent)
{
    m_device = appSettings().value(QStringLiteral("device/storageId")).toString().toLower();
    m_device.remove(QLatin1Char(':'));
    const QVariantMap capabilities = appSettings().value(QStringLiteral("device/capabilities")).toMap();
    m_estimateCalories = !capabilities.value(QStringLiteral("nativeCalories"), true).toBool();
    m_estimateActivity = !capabilities.value(QStringLiteral("nativeActivity"), true).toBool();
    m_ready = open();
}

Storage::~Storage()
{
    const QString connection = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
}

bool Storage::open()
{
    // БД кладём в «dotted»-каталог ~/.config/ru.nighteugene.aurorafitness/:
    // он в whitelist песочницы и переживает переустановку (в отличие от
    // ~/.config/<org>/<app>/, AppDataLocation и /srv/shared/<org>/<app>/).
    appSettingsEnsureDir();
    const QString dirPath = appConfigDir();
    if (dirPath.isEmpty()) {
        qWarning() << "Storage: нет пути к настройкам";
        return false;
    }
    QDir().mkpath(dirPath);

    const QString dbPath = dirPath + (m_device.isEmpty() ? QStringLiteral("/aurorafitness.db")
            : QStringLiteral("/device-%1.db").arg(m_device));

    // Разовая миграция со старых путей
    const QStringList oldPaths = {
        // До переезда в dotted-каталог БД лежала рядом с conf
        QDir::homePath() + QStringLiteral("/.config/ru.nighteugene/aurorafitness.db"),
        // Песоченый GUI до унификации путей держал БД в затираемом каталоге
        QDir::homePath() + QStringLiteral("/.config/ru.nighteugene/aurorafitness/aurorafitness.db"),
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/aurorafitness.db"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/aurorafitness.db"),
    };
    for (const QString &oldPath : oldPaths) {
        if (m_device.isEmpty() && !QFile::exists(dbPath) && oldPath != dbPath && QFile::exists(oldPath))
            QFile::copy(oldPath, dbPath);
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("aurorafitness-%1").arg(quintptr(this)));
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning() << "Storage: не удалось открыть БД:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    const QStringList ddl = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS daily_summary("
                       "ts INTEGER PRIMARY KEY, steps INT, calories INT,"
                       "resting_hr INT, max_hr INT, min_hr INT, avg_hr INT,"
                       "stress_avg INT, spo2_avg INT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS minute_samples("
                       "ts INTEGER PRIMARY KEY, steps INT, hr INT, spo2 INT, stress INT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS sleep_sessions("
                       "bed_time INTEGER PRIMARY KEY, wake_time INT, sleep_min INT,"
                       "deep_min INT, light_min INT, rem_min INT, awake_min INT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS step_observations("
                       "ts INTEGER PRIMARY KEY, seen_at INTEGER, steps INT, delta INT, active INT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS manual_samples("
                       "ts INTEGER, type TEXT, value INT, PRIMARY KEY(ts, type))"),
    };
    for (const QString &stmt : ddl) {
        if (!q.exec(stmt)) {
            qWarning() << "Storage: DDL:" << q.lastError().text();
            return false;
        }
    }

    // Миграция: раньше ключ daily_summary был fileId.timestamp (момент
    // генерации файла), из-за чего один день плодил строки. Оставляем по одной
    // (самой свежей) записи на день и нормализуем ключ к локальной полуночи.
    q.exec(QStringLiteral("DELETE FROM daily_summary WHERE ts NOT IN ("
                          "SELECT MAX(ts) FROM daily_summary "
                          "GROUP BY date(ts, 'unixepoch', 'localtime'))"));
    q.exec(QStringLiteral("UPDATE daily_summary SET ts = strftime('%s', "
                          "date(ts, 'unixepoch', 'localtime') || ' 00:00:00', 'utc')"));

    // Миграция: колонка activity_min (время активности, мин)
    QSqlQuery info(m_db);
    info.exec(QStringLiteral("PRAGMA table_info(daily_summary)"));
    bool hasActivityMin = false;
    while (info.next()) {
        if (info.value(1).toString() == QStringLiteral("activity_min")) {
            hasActivityMin = true;
            break;
        }
    }
    if (!hasActivityMin) {
        QSqlQuery alter(m_db);
        if (!alter.exec(QStringLiteral(
                    "ALTER TABLE daily_summary ADD COLUMN activity_min INT"))) {
            qWarning() << "Storage: миграция activity_min:" << alter.lastError().text();
            return false;
        }
    }
    return true;
}

void Storage::saveParsed(const QVariantMap &m)
{
    if (!m_ready)
        return;
    const QString kind = m.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("dailySummary"))
        saveDailySummary(m);
    else if (kind == QStringLiteral("dailyDetails"))
        saveDailyDetails(m);
    else if (kind == QStringLiteral("sleep"))
        saveSleep(m);
    else if (kind == QStringLiteral("manualSamples"))
        saveManualSamples(m);
}

void Storage::selectDevice(const QString &address)
{
    QString device = address.toLower();
    device.remove(QLatin1Char(':'));
    if (device == m_device)
        return;
    const QString connection = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
    m_device = device;
    m_ready = open();
    emit dataChanged();
}

void Storage::saveLiveReading(int steps, int heartRate)
{
    if (!m_ready)
        return;
    const qint64 now = QDateTime::currentDateTime().toTime_t();
    const QDate date = QDateTime::fromTime_t(uint(now)).date();
    const qint64 day = QDateTime(date).toTime_t();
    QVariantMap summary;
    summary.insert(QStringLiteral("timestamp"), now);
    if (steps >= 0) {
        summary.insert(QStringLiteral("steps"), steps);
        if (m_estimateCalories) {
            const QSettings settings = appSettings();
            const double weight = qBound(20.0, settings.value(QStringLiteral("profile/weightKg"), 70.0).toDouble(), 300.0);
            const double height = qBound(80.0, settings.value(QStringLiteral("profile/heightCm"), 170.0).toDouble(), 250.0);
            summary.insert(QStringLiteral("calories"), qRound(steps * height * 0.00415 * weight * 0.0005));
        }
        if (m_estimateActivity) {
            QSqlQuery previous(m_db);
            previous.prepare(QStringLiteral("SELECT ts, seen_at, steps, delta, active FROM step_observations WHERE ts>=? ORDER BY ts DESC LIMIT 1"));
            previous.addBindValue(day);
            int delta = 0;
            int active = 0;
            if (previous.exec() && previous.next()) {
                const qint64 seen = previous.value(1).toLongLong();
                if (now >= seen && now - seen <= 90 && steps > previous.value(2).toInt()) {
                    delta = steps - previous.value(2).toInt();
                    active = 1;
                }
                if (previous.value(0).toLongLong() == now / 60 * 60) {
                    delta += previous.value(3).toInt();
                    active = qMax(active, previous.value(4).toInt());
                }
            }
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("INSERT OR REPLACE INTO step_observations(ts, seen_at, steps, delta, active) VALUES(?, ?, ?, ?, ?)"));
            q.addBindValue(now / 60 * 60);
            q.addBindValue(now);
            q.addBindValue(steps);
            q.addBindValue(delta);
            q.addBindValue(active);
            if (!q.exec())
                qWarning() << "Step observation:" << q.lastError().text();
            q.prepare(QStringLiteral("SELECT COALESCE(SUM(active), 0) FROM step_observations WHERE ts>=? AND ts<?"));
            q.addBindValue(day);
            q.addBindValue(QDateTime(date.addDays(1)).toTime_t());
            if (q.exec() && q.next())
                summary.insert(QStringLiteral("activityMin"), q.value(0));
        }
    }
    if (heartRate > 0) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO manual_samples(ts, type, value) VALUES(?, 'hr', ?)"));
        q.addBindValue(now);
        q.addBindValue(heartRate);
        if (!q.exec()) {
            qWarning() << "Heart rate:" << q.lastError().text();
            return;
        }
        q.prepare(QStringLiteral("SELECT MIN(value), MAX(value), ROUND(AVG(value)) FROM manual_samples WHERE type='hr' AND ts>=? AND ts<?"));
        q.addBindValue(day);
        q.addBindValue(QDateTime(date.addDays(1)).toTime_t());
        if (q.exec() && q.next()) {
            summary.insert(QStringLiteral("minHr"), q.value(0));
            summary.insert(QStringLiteral("maxHr"), q.value(1));
            summary.insert(QStringLiteral("avgHr"), q.value(2));
        }
        q.prepare(QStringLiteral("UPDATE minute_samples SET hr=? WHERE ts=?"));
        q.addBindValue(heartRate);
        q.addBindValue(now / 60 * 60);
        if (q.exec() && q.numRowsAffected() == 0) {
            q.prepare(QStringLiteral("INSERT INTO minute_samples(ts, hr) VALUES(?, ?)"));
            q.addBindValue(now / 60 * 60);
            q.addBindValue(heartRate);
            if (!q.exec())
                qWarning() << "Heart rate sample:" << q.lastError().text();
        }
    }
    saveDailySummary(summary);
}

void Storage::setLiveEstimation(bool calories, bool activity)
{
    m_estimateCalories = calories;
    m_estimateActivity = activity;
    recalculateCalories();
}

void Storage::recalculateCalories()
{
    if (!m_ready || !m_estimateCalories)
        return;
    const QSettings settings = appSettings();
    const double weight = qBound(20.0, settings.value(QStringLiteral("profile/weightKg"), 70.0).toDouble(), 300.0);
    const double height = qBound(80.0, settings.value(QStringLiteral("profile/heightCm"), 170.0).toDouble(), 250.0);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE daily_summary SET calories=ROUND(steps * ?) WHERE steps IS NOT NULL"));
    q.addBindValue(height * 0.00415 * weight * 0.0005);
    if (!q.exec())
        qWarning() << "Estimated calories:" << q.lastError().text();
    emit dataChanged();
}

QVariantList Storage::hourlyActivity()
{
    QVariantList out;
    if (!m_ready || !m_estimateActivity)
        return out;
    const qint64 day = QDateTime(QDate::currentDate()).toTime_t();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT strftime('%H', ts, 'unixepoch', 'localtime'), SUM(active), SUM(delta) "
                             "FROM step_observations WHERE ts>=? AND ts<? GROUP BY 1 ORDER BY 1"));
    q.addBindValue(day);
    q.addBindValue(QDateTime(QDate::currentDate().addDays(1)).toTime_t());
    if (!q.exec())
        return out;
    while (q.next())
        out.append(QVariantMap{{QStringLiteral("hour"), q.value(0).toInt()},
                              {QStringLiteral("activeMin"), q.value(1).toInt()},
                              {QStringLiteral("steps"), q.value(2).toInt()}});
    return out;
}

void Storage::saveDailySummary(const QVariantMap &m)
{
    qlonglong ts = m.value(QStringLiteral("timestamp")).toLongLong();
    if (ts == 0)
        return;

    // fileId.timestamp у summary-файла — момент генерации файла, а не начало
    // дня: браслет при каждом синке шлёт новый ts. Нормализуем ключ к началу
    // дня, иначе один день множится в строках и на графике.
    ts = QDateTime(QDateTime::fromTime_t(uint(ts)).date()).toTime_t();

    // Собираем только присутствующие ключи — сначала UPDATE, чтобы не
    // затирать колонки, которых нет в этой порции данных
    QStringList sets, cols;
    QVariantList values;
    for (auto it = SUMMARY_COLUMNS.constBegin(); it != SUMMARY_COLUMNS.constEnd(); ++it) {
        if (!m.contains(it.key()))
            continue;
        sets << it.value() + QStringLiteral("=?");
        cols << it.value();
        values << m.value(it.key());
    }

    QSqlQuery q(m_db);
    if (!sets.isEmpty()) {
        q.prepare(QStringLiteral("UPDATE daily_summary SET %1 WHERE ts=?")
                  .arg(sets.join(QStringLiteral(", "))));
        for (const QVariant &v : values)
            q.addBindValue(v);
        q.addBindValue(ts);
        if (!q.exec())
            qWarning() << "Storage: update daily_summary:" << q.lastError().text();
    }
    if (sets.isEmpty() || q.numRowsAffected() == 0) {
        cols.prepend(QStringLiteral("ts"));
        values.prepend(ts);
        QStringList placeholders;
        for (int i = 0; i < cols.size(); ++i)
            placeholders << QStringLiteral("?");
        q.prepare(QStringLiteral("INSERT OR REPLACE INTO daily_summary(%1) VALUES(%2)")
                  .arg(cols.join(QStringLiteral(", ")),
                       placeholders.join(QStringLiteral(", "))));
        for (const QVariant &v : values)
            q.addBindValue(v);
        if (!q.exec()) {
            qWarning() << "Storage: insert daily_summary:" << q.lastError().text();
            return;
        }
    }
    emit dataChanged();
}

void Storage::saveDailyDetails(const QVariantMap &m)
{
    const QVariantList samples = m.value(QStringLiteral("samples")).toList();
    if (samples.isEmpty())
        return;

    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO minute_samples"
                             "(ts, steps, hr, spo2, stress) VALUES(?, ?, ?, ?, ?)"));
    int saved = 0;
    for (const QVariant &v : samples) {
        const QVariantMap s = v.toMap();
        const qlonglong ts = s.value(QStringLiteral("ts")).toLongLong();
        const qlonglong steps = s.value(QStringLiteral("steps")).toLongLong();
        const qlonglong hr = s.value(QStringLiteral("hr")).toLongLong();
        const qlonglong spo2 = s.value(QStringLiteral("spo2")).toLongLong();
        const qlonglong stress = s.value(QStringLiteral("stress")).toLongLong();
        // Нулевые заполнители браслета не сохраняем
        if (ts == 0 || (steps == 0 && hr == 0 && spo2 == 0 && stress == 0))
            continue;
        q.addBindValue(ts);
        q.addBindValue(s.contains(QStringLiteral("steps")) ? QVariant(steps) : QVariant());
        q.addBindValue(s.contains(QStringLiteral("hr")) ? QVariant(hr) : QVariant());
        q.addBindValue(s.contains(QStringLiteral("spo2")) ? QVariant(spo2) : QVariant());
        q.addBindValue(s.contains(QStringLiteral("stress")) ? QVariant(stress) : QVariant());
        if (!q.exec())
            qWarning() << "Storage: insert minute_samples:" << q.lastError().text();
        else
            ++saved;
    }
    m_db.commit();

    // «Время активности» браслета = число strength-минут в details-файле дня
    if (m.contains(QStringLiteral("strengthMinutes"))) {
        qlonglong dayTs = m.value(QStringLiteral("timestamp")).toLongLong();
        if (dayTs != 0) {
            // Ключ дня — локальная полночь (как в saveDailySummary)
            dayTs = QDateTime(QDateTime::fromTime_t(uint(dayTs)).date()).toTime_t();
            QSqlQuery uq(m_db);
            uq.prepare(QStringLiteral("UPDATE daily_summary SET activity_min=? WHERE ts=?"));
            uq.addBindValue(m.value(QStringLiteral("strengthMinutes")));
            uq.addBindValue(dayTs);
            if (!uq.exec())
                qWarning() << "Storage: update activity_min:" << uq.lastError().text();
            if (uq.numRowsAffected() == 0) {
                uq.prepare(QStringLiteral("INSERT OR REPLACE INTO daily_summary"
                                          "(ts, activity_min) VALUES(?, ?)"));
                uq.addBindValue(dayTs);
                uq.addBindValue(m.value(QStringLiteral("strengthMinutes")));
                if (!uq.exec())
                    qWarning() << "Storage: insert activity_min:" << uq.lastError().text();
            }
        }
    }

    if (saved > 0 || m.contains(QStringLiteral("strengthMinutes")))
        emit dataChanged();
}

void Storage::saveSleep(const QVariantMap &m)
{
    const qlonglong bedTime = m.value(QStringLiteral("bedTime")).toLongLong();
    const qlonglong wakeTime = m.value(QStringLiteral("wakeTime")).toLongLong();
    if (bedTime == 0)
        return;

    // Две формы: sleepMin рядом + summary{deep/light/rem/awakeMin}
    // или всё в summary{sleepMin,wakeMin,lightMin,remMin,deepMin}
    const QVariantMap summary = m.value(QStringLiteral("summary")).toMap();
    qlonglong sleepMin = m.value(QStringLiteral("sleepMin")).toLongLong();
    if (sleepMin == 0)
        sleepMin = summary.value(QStringLiteral("sleepMin")).toLongLong();
    qlonglong awakeMin = summary.value(QStringLiteral("awakeMin")).toLongLong();
    if (awakeMin == 0)
        awakeMin = summary.value(QStringLiteral("wakeMin")).toLongLong();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO sleep_sessions"
                             "(bed_time, wake_time, sleep_min, deep_min, light_min,"
                             "rem_min, awake_min) VALUES(?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(bedTime);
    q.addBindValue(wakeTime);
    q.addBindValue(sleepMin);
    q.addBindValue(summary.value(QStringLiteral("deepMin")).toLongLong());
    q.addBindValue(summary.value(QStringLiteral("lightMin")).toLongLong());
    q.addBindValue(summary.value(QStringLiteral("remMin")).toLongLong());
    q.addBindValue(awakeMin);
    if (!q.exec()) {
        qWarning() << "Storage: insert sleep_sessions:" << q.lastError().text();
        return;
    }
    emit dataChanged();
}

void Storage::saveManualSamples(const QVariantMap &m)
{
    const QVariantList samples = m.value(QStringLiteral("samples")).toList();
    if (samples.isEmpty())
        return;

    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO manual_samples(ts, type, value)"
                             " VALUES(?, ?, ?)"));
    int saved = 0;
    for (const QVariant &v : samples) {
        const QVariantMap s = v.toMap();
        const qlonglong ts = s.value(QStringLiteral("ts")).toLongLong();
        const QString type = s.value(QStringLiteral("type")).toString();
        if (ts == 0 || type.isEmpty())
            continue;
        q.addBindValue(ts);
        q.addBindValue(type);
        q.addBindValue(s.value(QStringLiteral("value")).toLongLong());
        if (!q.exec())
            qWarning() << "Storage: insert manual_samples:" << q.lastError().text();
        else
            ++saved;
    }
    m_db.commit();
    if (saved > 0)
        emit dataChanged();
}

QVariantMap Storage::todaySummary()
{
    QVariantMap out;
    if (!m_ready)
        return out;

    const QDateTime startOfDay(QDate::currentDate());
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ts, steps, calories, resting_hr, max_hr, min_hr,"
                             "avg_hr, stress_avg, spo2_avg, activity_min FROM daily_summary"
                             " WHERE ts >= ? AND ts < ? ORDER BY ts DESC LIMIT 1"));
    q.addBindValue(startOfDay.toTime_t());
    q.addBindValue(QDateTime(QDate::currentDate().addDays(1)).toTime_t());
    if (!q.exec() || !q.next())
        return out;

    static const QVector<QPair<QString, QString>> outKeys = {
        {QStringLiteral("steps"), QStringLiteral("steps")},
        {QStringLiteral("calories"), QStringLiteral("calories")},
        {QStringLiteral("resting_hr"), QStringLiteral("restingHr")},
        {QStringLiteral("max_hr"), QStringLiteral("maxHr")},
        {QStringLiteral("min_hr"), QStringLiteral("minHr")},
        {QStringLiteral("avg_hr"), QStringLiteral("avgHr")},
        {QStringLiteral("stress_avg"), QStringLiteral("stressAvg")},
        {QStringLiteral("spo2_avg"), QStringLiteral("spo2Avg")},
        {QStringLiteral("activity_min"), QStringLiteral("activityMin")},
    };
    out.insert(QStringLiteral("ts"), q.value(0).toLongLong());
    for (int col = 1; col <= outKeys.size(); ++col) {
        if (!q.value(col).isNull())
            out.insert(outKeys.at(col - 1).second, q.value(col).toLongLong());
    }
    return out;
}

QVariantList Storage::dailySummaries(int days)
{
    QVariantList out;
    if (!m_ready || days <= 0)
        return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ts, steps, calories, avg_hr FROM daily_summary"
                             " ORDER BY ts DESC LIMIT ?"));
    q.addBindValue(days);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("ts"), q.value(0).toLongLong());
        row.insert(QStringLiteral("steps"), q.value(1).toLongLong());
        row.insert(QStringLiteral("calories"), q.value(2).toLongLong());
        row.insert(QStringLiteral("avgHr"), q.value(3).toLongLong());
        out.prepend(row); // в ASC для графика
    }
    return out;
}

QVariantList Storage::sleepSessions(int limit)
{
    QVariantList out;
    if (!m_ready || limit <= 0)
        return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT bed_time, wake_time, sleep_min, deep_min,"
                             " light_min, rem_min, awake_min FROM sleep_sessions"
                             " ORDER BY bed_time DESC LIMIT ?"));
    q.addBindValue(limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("bedTime"), q.value(0).toLongLong());
        row.insert(QStringLiteral("wakeTime"), q.value(1).toLongLong());
        row.insert(QStringLiteral("sleepMin"), q.value(2).toLongLong());
        row.insert(QStringLiteral("deepMin"), q.value(3).toLongLong());
        row.insert(QStringLiteral("lightMin"), q.value(4).toLongLong());
        row.insert(QStringLiteral("remMin"), q.value(5).toLongLong());
        row.insert(QStringLiteral("awakeMin"), q.value(6).toLongLong());
        out.append(row);
    }
    return out;
}

QVariantList Storage::minuteSamples(qint64 fromTs, qint64 toTs)
{
    QVariantList out;
    if (!m_ready)
        return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ts, steps, hr FROM minute_samples"
                             " WHERE ts >= ? AND ts <= ? ORDER BY ts ASC"));
    q.addBindValue(fromTs);
    q.addBindValue(toTs);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("ts"), q.value(0).toLongLong());
        row.insert(QStringLiteral("steps"), q.value(1).toLongLong());
        row.insert(QStringLiteral("hr"), q.value(2).toLongLong());
        out.append(row);
    }
    return out;
}

int Storage::minuteSampleCount()
{
    if (!m_ready)
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM minute_samples")) || !q.next())
        return 0;
    return q.value(0).toInt();
}
