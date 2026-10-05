// SPDX-License-Identifier: BSD-3-Clause

#include "activityfetcher.h"
#include "xiaomichannel.h"
#include "activityparser.h"
#include "proto.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <algorithm>

namespace {
// CRC32 (IEEE, как java.util.zip.CRC32)
quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (char b : data)
        crc = table[(crc ^ quint8(b)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

quint16 le16(const QByteArray &d, int off)
{
    return quint16(quint8(d[off])) | (quint16(quint8(d[off + 1])) << 8);
}
}

ActivityFetcher::ActivityFetcher(XiaomiChannel *channel)
    : QObject(channel), m_channel(channel)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_timeout, &QTimer::timeout, this, &ActivityFetcher::onTimeout);
}

void ActivityFetcher::start()
{
    m_queue.clear();
    m_fileBuffer.clear();
    m_portionsTotal = m_portionsReceived = 0;

    // fetch-today: Health.activitySyncRequestToday{unknown1=0}
    pb::Writer req;
    req.varint(1, 0);
    pb::Writer health;
    health.msg(5, req);
    m_channel->sendHealthCommand(1, health.data);
    m_state = WaitTodayList;
    m_timeout.start();
    emit fetchProgress(QStringLiteral("Запрос списка файлов (сегодня)…"));
}

void ActivityFetcher::handleHealthResponse(quint32 subtype, const QByteArray &healthBytes)
{
    if (m_state == Idle || m_state == Done)
        return;

    if (subtype == 1 && m_state == WaitTodayList) {
        const QByteArray ids = pb::first(pb::parse(healthBytes), 2).bytes;
        for (int i = 0; i + 7 <= ids.size(); i += 7)
            m_queue.append(ids.mid(i, 7));
        qInfo() << "ActivityFetcher: файлов за сегодня:" << m_queue.size();

        // fetch-past — без полей
        m_channel->sendHealthCommand(2, QByteArray());
        m_state = WaitPastList;
        m_timeout.start();
        emit fetchProgress(QStringLiteral("Запрос списка файлов (архив)…"));
        return;
    }

    if (subtype == 2 && m_state == WaitPastList) {
        const QByteArray ids = pb::first(pb::parse(healthBytes), 2).bytes;
        int added = 0;
        for (int i = 0; i + 7 <= ids.size(); i += 7) {
            const QByteArray id = ids.mid(i, 7);
            if (!m_queue.contains(id)) {
                m_queue.append(id);
                ++added;
            }
        }
        qInfo() << "ActivityFetcher: файлов из архива:" << added;

        // Сортировка: timestamp, затем порядок SUMMARY→DETAILS→GPS
        std::sort(m_queue.begin(), m_queue.end(), [](const QByteArray &a, const QByteArray &b) {
            const xiaomiactivity::FileId fa = xiaomiactivity::parseFileId(a);
            const xiaomiactivity::FileId fb = xiaomiactivity::parseFileId(b);
            if (fa.timestamp != fb.timestamp)
                return fa.timestamp < fb.timestamp;
            return fetchOrder(fa.detailType) < fetchOrder(fb.detailType);
        });

        m_state = Fetching;
        requestNextFile();
        return;
    }
}

int ActivityFetcher::fetchOrder(quint8 detailType)
{
    switch (detailType) {
    case 1: return 0; // SUMMARY
    case 0: return 1; // DETAILS
    case 2: return 2; // GPS
    }
    return 3;
}

void ActivityFetcher::requestNextFile()
{
    while (!m_queue.isEmpty()) {
        m_currentFileId = m_queue.takeFirst();
        const xiaomiactivity::FileId id = xiaomiactivity::parseFileId(m_currentFileId);
        if (id.valid && (id.timestamp != 0 || id.version != 0))
            break;
        m_currentFileId.clear();
    }

    if (m_currentFileId.isEmpty()) {
        finish();
        return;
    }

    m_fileBuffer.clear();
    m_portionsTotal = m_portionsReceived = 0;

    pb::Writer health;
    health.bytes(2, m_currentFileId); // Health.activityRequestFileIds
    m_channel->sendHealthCommand(3, health.data);
    m_timeout.start();

    const xiaomiactivity::FileId id = xiaomiactivity::parseFileId(m_currentFileId);
    emit fetchProgress(QStringLiteral("Загрузка файла %1 (тип %2/%3, осталось %4)")
                       .arg(QDateTime::fromTime_t(id.timestamp).toString(QStringLiteral("dd.MM HH:mm")))
                       .arg(id.subtype).arg(id.detailType).arg(m_queue.size()));
}

void ActivityFetcher::addFilePortion(const QByteArray &portion)
{
    if (m_state != Fetching || portion.size() < 4)
        return;
    m_timeout.start(); // продлеваем таймаут

    const quint16 total = le16(portion, 0);
    const quint16 num = le16(portion, 2);
    if (num == 1) {
        m_fileBuffer.clear();
        m_portionsReceived = 0;
        m_portionsTotal = total;
    }
    if (total != m_portionsTotal || num != m_portionsReceived + 1)
        return; // рассинхрон — ждём таймаут и перезапрос

    m_fileBuffer += portion.mid(4);
    ++m_portionsReceived;

    if (m_portionsReceived < m_portionsTotal)
        return;

    // Файл собран: fileId(7) + pad(1) + payload + crc32(4)
    m_timeout.stop();
    QByteArray file = m_fileBuffer;
    m_fileBuffer.clear();

    bool crcOk = false;
    if (file.size() >= 13 && file.mid(0, 7) == m_currentFileId) {
        const quint32 expected = crc32(file.left(file.size() - 4));
        const QByteArray tail = file.right(4);
        const quint32 actual = quint32(quint8(tail[0])) | (quint32(quint8(tail[1])) << 8)
                | (quint32(quint8(tail[2])) << 16) | (quint32(quint8(tail[3])) << 24);
        crcOk = expected == actual;
        if (!crcOk)
            qWarning() << "ActivityFetcher: CRC32 не сошёлся";
    } else {
        qWarning() << "ActivityFetcher: файл битый или fileId не совпал";
    }

    if (crcOk) {
        // Отладочный дамп сырого файла для офлайн-анализа парсеров
        const QString dumpDir = QDir::homePath() + QStringLiteral("/activity_dumps");
        QDir().mkpath(dumpDir);
        QFile dump(dumpDir + QStringLiteral("/%1.bin").arg(QString::fromLatin1(m_currentFileId.toHex())));
        if (dump.open(QIODevice::WriteOnly))
            dump.write(file);

        const xiaomiactivity::FileId id = xiaomiactivity::parseFileId(m_currentFileId);
        const QVariantMap parsed = xiaomiactivity::parseActivityFile(
                    id, file.mid(8, file.size() - 12));
        emit fileParsed(parsed);
        ackFile(m_currentFileId);
    }

    m_currentFileId.clear();
    requestNextFile();
}

void ActivityFetcher::ackFile(const QByteArray &fileId)
{
    pb::Writer health;
    health.bytes(3, fileId); // Health.activitySyncAckFileIds
    m_channel->sendHealthCommand(5, health.data);
}

void ActivityFetcher::onTimeout()
{
    if (m_state == Fetching && !m_currentFileId.isEmpty()) {
        qWarning() << "ActivityFetcher: таймаут файла, пропускаем";
        m_currentFileId.clear();
        m_fileBuffer.clear();
        m_portionsTotal = m_portionsReceived = 0;
        requestNextFile();
        return;
    }
    if (m_state == WaitTodayList || m_state == WaitPastList) {
        qWarning() << "ActivityFetcher: таймаут списка файлов";
        finish();
    }
}

void ActivityFetcher::finish()
{
    m_state = Done;
    m_timeout.stop();
    emit fetchProgress(QStringLiteral("Данные обновлены"));
    emit finished();
}
