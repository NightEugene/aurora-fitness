// SPDX-License-Identifier: BSD-3-Clause

#include "dataupload.h"
#include "xiaomichannel.h"
#include "proto.h"

#include <QCryptographicHash>
#include <QDebug>

namespace {
// Поля DataUpload
const int F_DATA_UPLOAD_REQUEST = 1;
const int F_DATA_UPLOAD_ACK = 2;
// Поля DataUploadAck
const int F_ACK_MD5 = 1;
const int F_ACK_ERRNO = 2;
const int F_ACK_RESUME_POSITION = 4;
const int F_ACK_CHUNK_SIZE = 5;

const quint32 CMD_UPLOAD_START = 0;

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

void appendLe16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

void appendLe32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 24) & 0xff));
}

quint16 le16(const QByteArray &d, int off)
{
    return quint16(quint8(d[off])) | (quint16(quint8(d[off + 1])) << 8);
}
}

DataUpload::DataUpload(XiaomiChannel *channel)
    : QObject(channel), m_channel(channel)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(5000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        finish(false, QStringLiteral("таймаут (state=%1)").arg(int(m_state)));
    });
}

void DataUpload::startUpload(quint8 type, const QByteArray &bytes)
{
    if (m_state != State::Idle) {
        qWarning() << "DataUpload: занят, отклоняю загрузку типа" << type;
        return;
    }

    m_chunkSize = 2048;
    m_type = type;
    m_bytes = bytes;
    m_md5 = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);

    qInfo() << "DataUpload: запрос загрузки" << bytes.size() << "байт, тип" << type
            << "md5" << m_md5.toHex();

    pb::Writer req;
    req.varint(1, type);        // DataUploadRequest.type
    req.bytes(2, m_md5);        // DataUploadRequest.md5sum
    req.varint(3, bytes.size()); // DataUploadRequest.size

    pb::Writer du;
    du.msg(F_DATA_UPLOAD_REQUEST, req);

    m_channel->sendDataUploadCommand(du.data);
    m_state = State::WaitUploadAck;
    m_timeout.start();
}

void DataUpload::handleCommand(quint32 subtype, const QByteArray &dataUploadProto)
{
    if (subtype != CMD_UPLOAD_START || m_state != State::WaitUploadAck)
        return;

    const QList<pb::Field> duFields = pb::parse(dataUploadProto);
    const pb::Field ack = pb::first(duFields, F_DATA_UPLOAD_ACK);
    if (ack.bytes.isEmpty()) {
        finish(false, QStringLiteral("в ответе нет dataUploadAck"));
        return;
    }

    const QList<pb::Field> ackFields = pb::parse(ack.bytes);
    if (ackFields.isEmpty()) {
        finish(false, QStringLiteral("повреждённый dataUploadAck"));
        return;
    }
    const QByteArray ackMd5 = pb::first(ackFields, F_ACK_MD5).bytes;
    const quint64 errNo = pb::first(ackFields, F_ACK_ERRNO).varint;
    const quint32 resumePosition = quint32(pb::first(ackFields, F_ACK_RESUME_POSITION).varint);
    const pb::Field chunkSizeField = pb::first(ackFields, F_ACK_CHUNK_SIZE);

    qInfo() << "DataUpload: ack errno" << errNo << "resume" << resumePosition
            << "chunkSize" << chunkSizeField.varint << "md5" << ackMd5.toHex();

    if (errNo != 0) {
        // errno=1 наблюдался при заполненном хранилище иконок браслета;
        // загрузка не состоялась, пакет не помечаем обслуженным.
        qInfo() << "DataUpload: браслет отклонил загрузку типа" << m_type
                << "(errno" << errNo << ")";
        m_timeout.stop();
        m_state = State::Idle;
        m_bytes.clear();
        emit uploadFinished(false);
        return;
    }

    if (chunkSizeField.varint > 0) {
        if (chunkSizeField.varint <= 4 || chunkSizeField.varint > 65535) {
            finish(false, QStringLiteral("некорректный chunkSize"));
            return;
        }
        m_chunkSize = quint32(chunkSizeField.varint);
    }
    if (pb::first(ackFields, F_ACK_RESUME_POSITION).varint > quint64(m_bytes.size())
            || (!ackMd5.isEmpty() && ackMd5 != m_md5)) {
        finish(false, QStringLiteral("неверный resumePosition или md5"));
        return;
    }

    buildPayload(resumePosition);

    const int partSize = int(m_chunkSize) - 4; // 4 байта заголовка в каждой части
    m_totalParts = (m_payload.size() + partSize - 1) / partSize;
    if (m_totalParts > 65535) {
        finish(false, QStringLiteral("слишком много частей"));
        return;
    }
    m_currentPart = 0;
    qInfo() << "DataUpload: payload" << m_payload.size() << "байт,"
            << m_totalParts << "частей по" << partSize;
    sendNextPart();
}

void DataUpload::buildPayload(quint32 resumePosition)
{
    m_payload.clear();
    m_payload.reserve(2 + 16 + 4 + m_bytes.size() + 4);
    m_payload.append(char(0));
    m_payload.append(char(m_type));
    m_payload += m_md5;
    appendLe32(m_payload, quint32(m_bytes.size()));
    if (int(resumePosition) < m_bytes.size())
        m_payload += m_bytes.mid(int(resumePosition));
    appendLe32(m_payload, crc32(m_payload));
}

void DataUpload::sendNextPart()
{
    const int partSize = int(m_chunkSize) - 4;
    const int start = m_currentPart * partSize;
    const int end = qMin(start + partSize, m_payload.size());

    QByteArray part;
    appendLe16(part, quint16(m_totalParts));
    appendLe16(part, quint16(m_currentPart + 1));
    part += m_payload.mid(start, end - start);

    // Характеристика 0x0055: шифрование всегда с counter=0 (incrementNonce=false в GB)
    const QByteArray enc = m_channel->encryptUploadPayload(part);
    const int maxWrite = m_channel->uploadWriteSize();

    if (enc.size() + 6 <= maxWrite) {
        // Часть умещается в одиночный кадр: 00 00 02 01 <counter=0 u16le> <cipher>
        QByteArray frame("\x00\x00\x02\x01\x00\x00", 6);
        frame += enc;
        m_txChunks.clear();
        m_channel->writeUploadChar(frame);
        m_state = State::WaitSingleAck;
    } else {
        // Chunked-передача: заголовок 00 00 00 01 <numChunks u16le>
        const int chunkPayload = maxWrite - 2; // 2 байта — номер чанка
        const int numChunks = (enc.size() + chunkPayload - 1) / chunkPayload;
        m_txChunks.clear();
        for (int i = 0; i < numChunks; ++i) {
            const int cs = i * chunkPayload;
            const int ce = qMin(cs + chunkPayload, enc.size());
            QByteArray c;
            appendLe16(c, quint16(i + 1));
            c += enc.mid(cs, ce - cs);
            m_txChunks.append(c);
        }

        QByteArray header("\x00\x00\x00\x01", 4);
        appendLe16(header, quint16(numChunks));
        m_channel->writeUploadChar(header);
        m_state = State::WaitChunkStartAck;
        qInfo() << "DataUpload: часть" << m_currentPart + 1 << "из" << m_totalParts
                << "-" << numChunks << "чанков";
    }
    m_timeout.start();
}

void DataUpload::sendChunks(const QList<int> &indices)
{
    if (indices.isEmpty()) {
        for (const QByteArray &c : m_txChunks)
            m_channel->writeUploadChar(c);
        return;
    }
    for (int idx : indices) {
        if (idx >= 1 && idx <= m_txChunks.size())
            m_channel->writeUploadChar(m_txChunks.at(idx - 1));
    }
}

void DataUpload::handleCharPacket(const QByteArray &packet)
{
    if (packet.size() < 4)
        return;

    const quint16 chunkNo = le16(packet, 0);
    if (chunkNo != 0)
        return; // входящих chunked-передач на этой характеристике не ожидаем

    const quint8 type = quint8(packet[2]);
    const quint8 sub = quint8(packet[3]);

    if (type == 0x03) {
        // ACK/NACK на одиночный кадр
        if (m_state == State::WaitSingleAck) {
            if (sub == 0)
                partDone();
            else
                finish(false, QStringLiteral("NACK (%1) на одиночный кадр").arg(sub));
        }
        return;
    }

    if (type != 0x01)
        return;

    switch (sub) {
    case 0x00: // chunked ack end
        if (m_state == State::WaitChunkEndAck)
            partDone();
        break;
    case 0x01: // chunked ack start — шлём чанки
        if (m_state == State::WaitChunkStartAck) {
            sendChunks(QList<int>());
            m_state = State::WaitChunkEndAck;
            m_timeout.start();
        }
        break;
    case 0x02: // chunked nack
        finish(false, QStringLiteral("chunked NACK"));
        break;
    case 0x05: {
        // Браслет просит переслать отдельные чанки (список u16le после заголовка)
        QList<int> missing;
        for (int i = 4; i + 1 < packet.size(); i += 2)
            missing.append(int(le16(packet, i)));
        qInfo() << "DataUpload: пересылка чанков" << missing;
        if (m_state == State::WaitChunkEndAck || m_state == State::WaitChunkStartAck) {
            sendChunks(missing);
            m_state = State::WaitChunkEndAck;
            m_timeout.start();
        }
        break;
    }
    default:
        qWarning() << "DataUpload: неизвестный chunked ack" << sub;
        break;
    }
}

void DataUpload::partDone()
{
    ++m_currentPart;
    if (m_currentPart >= m_totalParts) {
        finish(true);
        return;
    }
    sendNextPart();
}

void DataUpload::finish(bool success, const QString &reason)
{
    if (success)
        qInfo() << "DataUpload: загрузка завершена, тип" << m_type;
    else
        qWarning() << "DataUpload: загрузка НЕ удалась, тип" << m_type << "-" << reason;

    m_timeout.stop();
    m_state = State::Idle;
    m_bytes.clear();
    m_payload.clear();
    m_txChunks.clear();
    emit uploadFinished(success);
}
