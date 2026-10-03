// SPDX-License-Identifier: BSD-3-Clause

#ifndef XIAOMI_DATAUPLOAD_H
#define XIAOMI_DATAUPLOAD_H

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QTimer>

// Загрузка бинарных данных на браслет (type=22): watchface, иконки уведомлений.
// Портировано из Gadgetbridge (XiaomiDataUploadService + chunked-протокол
// XiaomiCharacteristicV1 для характеристики 0x0055).
class DataUpload : public QObject
{
    Q_OBJECT
public:
    // DataUploadRequest.type
    static const quint8 TYPE_WATCHFACE = 16;
    static const quint8 TYPE_FIRMWARE = 32;
    static const quint8 TYPE_NOTIFICATION_ICON = 50;

    explicit DataUpload(class XiaomiChannel *channel);

    bool isIdle() const { return m_state == State::Idle; }

    // Запросить загрузку; дальше всё идёт по ответам браслета
    void startUpload(quint8 type, const QByteArray &bytes);

    // Ответ браслета type=22 (Command.subtype, содержимое поля Command.dataUpload)
    void handleCommand(quint32 subtype, const QByteArray &dataUploadProto);

    // Кадры с upload-характеристики 0x0055 (chunked/single ack)
    void handleCharPacket(const QByteArray &packet);

signals:
    void uploadFinished(bool success);

private:
    enum class State {
        Idle,
        WaitUploadAck,     // ждём DataUploadAck на requestUpload
        WaitChunkStartAck, // отправили заголовок chunked-передачи части
        WaitChunkEndAck,   // отправили все чанки части
        WaitSingleAck      // часть уместилась в один кадр
    };

    void buildPayload(quint32 resumePosition);
    void sendNextPart();
    void sendChunks(const QList<int> &indices); // indices 1-based; пустой = все
    void partDone();
    void finish(bool success, const QString &reason = QString());

    XiaomiChannel *m_channel;
    QTimer m_timeout;
    State m_state = State::Idle;

    quint8 m_type = 0;
    QByteArray m_bytes;
    QByteArray m_md5;

    quint32 m_chunkSize = 2048;   // из DataUploadAck; на Mi Band 8 поля нет — 2048
    QByteArray m_payload;         // 0x00,type,md5,size,bytes,crc32
    int m_totalParts = 0;
    int m_currentPart = 0;        // 0-based

    QList<QByteArray> m_txChunks; // чанки текущей части (для пересылки по запросу)
};

#endif // XIAOMI_DATAUPLOAD_H
