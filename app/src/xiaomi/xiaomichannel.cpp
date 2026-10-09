// SPDX-License-Identifier: BSD-3-Clause

#include "xiaomichannel.h"
#include "activityfetcher.h"
#include "dataupload.h"
#include "proto.h"
#include "crypto.h"
#include "../appsettings.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>

namespace {
const char BLUEZ[] = "org.bluez";
const char IFACE_CHAR[] = "org.bluez.GattCharacteristic1";
const char IFACE_PROPS[] = "org.freedesktop.DBus.Properties";

// Command.type
const quint32 TYPE_AUTH = 1;
const quint32 TYPE_SYSTEM = 2;
const quint32 TYPE_HEALTH = 8;
const quint32 TYPE_NOTIFICATION = 7; // не 9: 9 — номер поля notification в Command
const quint32 TYPE_DATA_UPLOAD = 22;
// Account / Auth
const int F_AUTH = 3;           // Command.auth
const int F_APP_VERIFY = 30;    // Account.appVerify
const int F_DEVICE_VERIFY = 31; // Account.deviceVerify
const int F_APP_CONFIRM = 32;   // Account.appConfirm
const int F_AUTH_STEP4 = 33;    // Account.authStep4
// System
const int F_SYSTEM = 4;         // Command.system
const int F_POWER = 2;          // System.power
const int F_DEVICE_INFO = 3;    // System.deviceInfo
const int F_CLOCK = 4;          // System.clock
const int F_HEALTH = 10;        // Command.health
const int F_NOTIFICATION = 9;   // Command.notification
const int F_DATA_UPLOAD = 24;   // Command.dataUpload
// Notification
const int F_NOTIFICATION2 = 3;  // Notification.notification2
const int F_NOTIFICATION3 = 1;  // Notification2.notification3
const int F_NOTIF_ICON_REPLY = 14;    // Notification.notificationIconReply
const int F_NOTIF_ICON_REQUEST = 15;  // Notification.notificationIconRequest
const int F_NOTIF_ICON_QUERY = 16;    // Notification.notificationIconQuery
// Notification subtype
const quint32 CMD_NOTIFICATION_ICON_REQUEST = 15;
const quint32 CMD_NOTIFICATION_ICON_QUERY = 16;
}

XiaomiChannel::XiaomiChannel(QObject *parent) : WearableChannel(parent)
{
    m_receiveTimer.setInterval(1000);
    connect(&m_receiveTimer, &QTimer::timeout, this, [this]() {
        for (auto it = m_incoming.begin(); it != m_incoming.end();) {
            if (it->age.elapsed() > 5000) it = m_incoming.erase(it);
            else ++it;
        }
        if (m_incoming.isEmpty()) m_receiveTimer.stop();
    });
}

void XiaomiChannel::setup(const QString &readCharPath, const QString &writeCharPath,
                          const QString &activityCharPath, const QString &dataUploadCharPath)
{
    m_readPath = readCharPath;
    m_writePath = writeCharPath;
    m_activityPath = activityCharPath;
    m_uploadPath = dataUploadCharPath;

    if (!m_uploadPath.isEmpty() && !m_uploader) {
        m_uploader = new DataUpload(this);
        connect(m_uploader, &DataUpload::uploadFinished, this, [this](bool success) {
            qInfo() << "XiaomiChannel: загрузка данных на браслет:" << (success ? "OK" : "ОШИБКА");
            // Пакет помечаем обслуженным только при успехе — при errno=1
            // (нет слотов) дадим повторить попытку на следующее уведомление
            if (!m_iconUploading.isEmpty()) {
                if (success)
                    m_iconServed.insert(m_iconUploading + QStringLiteral(":")
                                        + QString::number(m_iconUploadingSize));
                m_iconUploading.clear();
            }
        });
    }
}

void XiaomiChannel::setAuthKey(const QByteArray &key16)
{
    m_authKey = key16;
}

void XiaomiChannel::startNotify(const QString &charPath)
{
    QDBusInterface chrc(QString::fromLatin1(BLUEZ), charPath,
                        QString::fromLatin1(IFACE_CHAR), QDBusConnection::systemBus());
    QDBusReply<void> reply = chrc.call(QStringLiteral("StartNotify"));
    if (!reply.isValid())
        qWarning() << "StartNotify" << charPath << reply.error().message();
}

void XiaomiChannel::writeValue(const QString &charPath, const QByteArray &value)
{
    PendingWrite w;
    w.path = charPath;
    w.value = value;
    m_writeQueue.append(w);
    pumpWriteQueue();
}

void XiaomiChannel::pumpWriteQueue()
{
    if (m_writeBusy || m_writeQueue.isEmpty())
        return;
    m_writeBusy = true;
    const PendingWrite w = m_writeQueue.first();

    // Этот BlueZ эхом отражает наши записи как PropertiesChanged(Value) —
    // запоминаем, чтобы onCharacteristicValue их отбросил
    m_ownWrites.append(qMakePair(w.path, w.value));
    if (m_ownWrites.size() > 64)
        m_ownWrites.removeFirst();

    QDBusInterface *chrc = new QDBusInterface(QString::fromLatin1(BLUEZ), w.path,
                                              QString::fromLatin1(IFACE_CHAR),
                                              QDBusConnection::systemBus(), this);
    QVariantMap options;
    QDBusPendingCall call = chrc->asyncCall(QStringLiteral("WriteValue"),
                                            QVariant::fromValue(w.value),
                                            QVariant::fromValue(options));
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, chrc, &QObject::deleteLater);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *wt) {
        wt->deleteLater();
        const QDBusPendingReply<void> reply = *wt;
        m_writeBusy = false;
        if (m_writeQueue.isEmpty())
            return;
        if (reply.isError()) {
            // BlueZ занят предыдущей записью — ретрай через 100 мс
            if (reply.error().name().contains(QStringLiteral("InProgress"))
                    && m_writeQueue.first().retries < 10) {
                m_writeQueue.first().retries++;
                QTimer::singleShot(100, this, [this]() { pumpWriteQueue(); });
                return;
            }
            qWarning() << "WriteValue" << m_writeQueue.first().path
                       << reply.error().message();
        }
        m_writeQueue.removeFirst();
        pumpWriteQueue();
    });
}

void XiaomiChannel::sendAck(const QString &charPath)
{
    writeValue(charPath, QByteArray("\x00\x00\x03\x00", 4));
}

void XiaomiChannel::startAuth()
{
    if (m_authKey.size() != 16) {
        emit authFailed(QStringLiteral("Auth key не задан или неверной длины (нужно 16 байт hex)"));
        return;
    }
    if (m_readPath.isEmpty() || m_writePath.isEmpty()) {
        emit authFailed(QStringLiteral("Характеристики FE95 не найдены"));
        return;
    }

    startNotify(m_readPath);
    startNotify(m_writePath);
    if (!m_activityPath.isEmpty())
        startNotify(m_activityPath);
    if (!m_uploadPath.isEmpty())
        startNotify(m_uploadPath);

    m_state = State::Idle;
    m_authed = false;
    // Новая BT-сессия: браслет мог перезагрузиться (таблица иконок
    // очищается ребутом) — разрешаем повторную загрузку иконок
    m_iconServed.clear();
    m_iconUploading.clear();
    emit authStatusChanged(QStringLiteral("Аутентификация…"));
    sendAppNonce();
}

// --- построение protobuf-команд ---

static QByteArray buildCommand(quint32 type, quint32 subtype, int bodyField, const pb::Writer &body)
{
    pb::Writer cmd;
    cmd.varint(1, type);
    cmd.varint(2, subtype);
    if (bodyField > 0)
        cmd.msg(bodyField, body);
    return cmd.data;
}

void XiaomiChannel::sendAppNonce()
{
    m_phoneNonce = xcrypto::randomBytes(16);
    if (m_phoneNonce.size() != 16) {
        emit authFailed(QStringLiteral("Не удалось создать nonce аутентификации"));
        return;
    }

    pb::Writer appVerify;
    appVerify.bytes(1, m_phoneNonce);      // AppVerify.nonce

    pb::Writer account;
    account.msg(F_APP_VERIFY, appVerify);  // Account.appVerify

    sendPlainCommand(buildCommand(TYPE_AUTH, 26, F_AUTH, account));
    m_state = State::WaitWatchNonce;
}

void XiaomiChannel::handleWatchNonce(const QByteArray &authMsg)
{
    const QList<pb::Field> accountFields = pb::parse(authMsg);
    const pb::Field deviceVerify = pb::first(accountFields, F_DEVICE_VERIFY);
    if (deviceVerify.bytes.isEmpty()) {
        emit authFailed(QStringLiteral("В ответе нет deviceVerify"));
        return;
    }
    const QList<pb::Field> dvFields = pb::parse(deviceVerify.bytes);
    m_watchNonce = pb::first(dvFields, 1).bytes;
    const QByteArray watchHmac = pb::first(dvFields, 2).bytes;
    if (m_watchNonce.size() != 16 || watchHmac.size() != 32) {
        emit authFailed(QStringLiteral("Неверный размер nonce/hmac от браслета"));
        return;
    }

    m_decryptionKey.clear();
    m_encryptionKey.clear();
    deriveKeys();
    if (m_decryptionKey.size() != 16 || m_encryptionKey.size() != 16) {
        emit authFailed(QStringLiteral("Не удалось получить ключи сеанса"));
        return;
    }

    const QByteArray expected = xcrypto::hmacSha256(m_decryptionKey, m_watchNonce + m_phoneNonce);
    if (expected != watchHmac) {
        emit authFailed(QStringLiteral("HMAC браслета не сошёлся — неверный auth key"));
        return;
    }

    sendAppConfirm();
}

void XiaomiChannel::deriveKeys()
{
    // K = HMAC-SHA256(key = phoneNonce||watchNonce, msg = authKey)
    const QByteArray k = xcrypto::hmacSha256(m_phoneNonce + m_watchNonce, m_authKey);
    if (k.size() != 32) return;
    const QByteArray info = QByteArray("miwear-auth");
    const QByteArray t1 = xcrypto::hmacSha256(k, info + '\x01');
    const QByteArray t2 = xcrypto::hmacSha256(k, t1 + info + '\x02');
    if (t1.size() != 32 || t2.size() != 32) return;
    const QByteArray out = t1 + t2;

    m_decryptionKey = out.mid(0, 16);
    m_encryptionKey = out.mid(16, 16);
    m_decryptionNonce4 = out.mid(32, 4);
    m_encryptionNonce4 = out.mid(36, 4);
}

QByteArray XiaomiChannel::makeNonce(const QByteArray &nonce4, quint32 counter)
{
    QByteArray nonce(12, 0);
    if (nonce4.size() != 4)
        return QByteArray();
    memcpy(nonce.data(), nonce4.constData(), 4);
    nonce[8] = char(counter & 0xff);
    nonce[9] = char((counter >> 8) & 0xff);
    nonce[10] = char((counter >> 16) & 0xff);
    nonce[11] = char((counter >> 24) & 0xff);
    return nonce;
}

void XiaomiChannel::sendAppConfirm()
{
    // CompanionDevice
    pb::Writer companion;
    companion.varint(1, 0);            // unknown1, required — сериализуем явно
    companion.fixedFloat(2, 33.0f);    // phoneApiLevel
    companion.str(3, QStringLiteral("Aurora Phone"));
    companion.varint(4, 224);          // unknown3
    companion.str(5, QStringLiteral("RU"));

    const QByteArray encryptedNonces = xcrypto::hmacSha256(m_encryptionKey,
                                                           m_phoneNonce + m_watchNonce);
    const QByteArray encryptedDeviceInfo = xcrypto::aesCcmEncrypt(
                m_encryptionKey, makeNonce(m_encryptionNonce4, 0), companion.data);

    if (encryptedNonces.size() != 32 || encryptedDeviceInfo.isEmpty()) {
        emit authFailed(QStringLiteral("Не удалось зашифровать подтверждение аутентификации"));
        return;
    }
    pb::Writer appConfirm;
    appConfirm.bytes(1, encryptedNonces);
    appConfirm.bytes(2, encryptedDeviceInfo);

    pb::Writer account;
    account.msg(F_APP_CONFIRM, appConfirm);

    // Кадр plaintext (поля зашифрованы внутри)
    sendPlainCommand(buildCommand(TYPE_AUTH, 27, F_AUTH, account));
    m_state = State::WaitConfirm;
}

// --- транспорт ---

void XiaomiChannel::sendPlainCommand(const QByteArray &proto)
{
    QByteArray frame;
    frame.append("\x00\x00\x02\x02", 4); // одиночная plaintext-команда
    frame += proto;
    writeValue(m_writePath, frame);
}

void XiaomiChannel::sendEncryptedCommand(const QByteArray &proto)
{
    const quint32 counter = m_encCounter++;
    const QByteArray cipherTag = xcrypto::aesCcmEncrypt(
                m_encryptionKey, makeNonce(m_encryptionNonce4, counter), proto);

    if (cipherTag.isEmpty()) {
        emit error(QStringLiteral("Не удалось зашифровать команду"));
        return;
    }
    QByteArray frame;
    frame.append("\x00\x00\x02\x01", 4); // одиночная зашифрованная команда
    frame.append(char(counter & 0xff));
    frame.append(char((counter >> 8) & 0xff));
    frame += cipherTag;
    writeValue(m_writePath, frame);
}

void XiaomiChannel::onCharacteristicValue(const QString &path, const QByteArray &value)
{
    if (path != m_readPath && path != m_writePath && path != m_activityPath
            && path != m_uploadPath)
        return;
    // Эхо наших собственных записей (PropertiesChanged Value на write-char)
    if (!m_ownWrites.isEmpty()) {
        for (int i = 0; i < m_ownWrites.size(); ++i) {
            if (m_ownWrites.at(i).first == path && m_ownWrites.at(i).second == value) {
                m_ownWrites.removeAt(i);
                return;
            }
        }
    }
    // На этой связке BlueZ/dbus/QtDBus каждый сигнал Value доезжает до слота
    // дважды (на шине сигнал один — проверено dbus-monitor). Отбрасываем
    // повтор с тем же содержимым в коротком окне — ретрансмиты браслета
    // приходят с заметно большей паузой.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (path == m_lastRxPath && value == m_lastRxValue && now - m_lastRxMs < 200)
        return;
    m_lastRxPath = path;
    m_lastRxValue = value;
    m_lastRxMs = now;
    handlePacket(path, value);
}

void XiaomiChannel::handlePacket(const QString &charPath, const QByteArray &packet)
{
    if (packet.size() < 2)
        return;

    // ACK-и нашей исходящей передачи на upload-характеристике — в DataUpload
    if (charPath == m_uploadPath && m_uploader && packet.size() >= 4
            && packet[0] == 0 && packet[1] == 0
            && (packet[2] == 0x01 || packet[2] == 0x03)) {
        m_uploader->handleCharPacket(packet);
        return;
    }

    const quint16 chunkNo = quint8(packet[0]) | (quint16(quint8(packet[1])) << 8);

    if (chunkNo != 0) {
        // Данные чанка входящего chunked-сообщения
        auto it = m_incoming.find(charPath);
        if (it != m_incoming.end() && chunkNo <= it->expected) {
            if (it->chunks.contains(chunkNo)) return;
            if (packet.size() - 2 > 1024 * 1024 - it->bytes) {
                m_incoming.erase(it);
                emit error(QStringLiteral("Входящий пакет превышает допустимый размер"));
                return;
            }
            it->bytes += packet.size() - 2;
            it->chunks.insert(chunkNo, packet.mid(2));
            it->age.restart();
            if (it->chunks.size() == it->expected) {
                QByteArray assembled;
                assembled.reserve(it->bytes);
                for (int i = 1; i <= it->expected; ++i) assembled += it->chunks.value(quint16(i));
                const bool encrypted = it->encrypted;
                m_incoming.erase(it);
                writeValue(charPath, QByteArray("\x00\x00\x01\x00", 4));
                if (!assembled.isEmpty()) deliverPayload(charPath, encrypted, assembled);
            }
        }
        return;
    }

    if (packet.size() < 3)
        return;
    const quint8 type = quint8(packet[2]);

    switch (type) {
    case 0x00: {
        // Запрос начала chunked-передачи от браслета: 00 00 00 <enc> <numChunks u16le>
        if (packet.size() >= 6) {
            Incoming incoming;
            incoming.encrypted = packet[3] == 0x01;
            incoming.expected = quint8(packet[4]) | (quint16(quint8(packet[5])) << 8);
            if (incoming.expected == 0 || incoming.expected > 4096) return;
            incoming.age.start();
            m_incoming.insert(charPath, incoming);
            m_receiveTimer.start();
            writeValue(charPath, QByteArray("\x00\x00\x01\x01", 4)); // start ack
        }
        break;
    }
    case 0x01:
        // chunked ack от браслета (на нашу исходящую передачу) — игнорируем
        break;
    case 0x02: {
        // Одиночная команда: 00 00 02 <enc> <payload>
        sendAck(charPath);
        if (packet.size() > 4)
            deliverPayload(charPath, packet[3] == 0x01, packet.mid(4));
        break;
    }
    case 0x03:
        // ACK на нашу запись — ничего не делаем
        break;
    default:
        qWarning() << "XiaomiChannel: неизвестный кадр" << packet.toHex();
        break;
    }
}

void XiaomiChannel::deliverPayload(const QString &charPath, bool encrypted, const QByteArray &payload)
{
    QByteArray plain = payload;
    if (encrypted) {
        bool ok = false;
        // Входящие дешифруются с counter=0
        plain = xcrypto::aesCcmDecrypt(m_decryptionKey,
                                       makeNonce(m_decryptionNonce4, 0), payload, &ok);
        if (!ok) {
            qWarning() << "XiaomiChannel: не удалось расшифровать пакет";
            return;
        }
    }

    if (charPath == m_activityPath) {
        if (m_fetcher)
            m_fetcher->addFilePortion(plain);
        return;
    }
    processCommand(plain);
}

void XiaomiChannel::processCommand(const QByteArray &plain)
{
    const QList<pb::Field> cmdFields = pb::parse(plain);
    const quint32 type = quint32(pb::first(cmdFields, 1).varint);
    const quint32 subtype = quint32(pb::first(cmdFields, 2).varint);
    qInfo() << "XiaomiChannel: команда type" << type << "subtype" << subtype;

    if (type == TYPE_AUTH && subtype == 26 && m_state == State::WaitWatchNonce) {
        handleWatchNonce(pb::first(cmdFields, F_AUTH).bytes);
    } else if (type == TYPE_AUTH && subtype == 27 && m_state == State::WaitConfirm) {
        m_state = State::Authed;
        m_authed = true;
        m_encCounter = 1;
        emit authStatusChanged(QStringLiteral("Аутентификация успешна"));
        emit authenticated();
        sendCurrentTime();
        requestBattery();
        requestDeviceInfo();
    } else if (type == TYPE_SYSTEM) {
        handleSystem(pb::first(cmdFields, F_SYSTEM).bytes);
    } else if (type == TYPE_HEALTH) {
        if (m_fetcher)
            m_fetcher->handleHealthResponse(subtype, pb::first(cmdFields, F_HEALTH).bytes);
    } else if (type == TYPE_NOTIFICATION) {
        handleNotification(subtype, pb::first(cmdFields, F_NOTIFICATION).bytes);
    } else if (type == TYPE_DATA_UPLOAD) {
        if (m_uploader)
            m_uploader->handleCommand(subtype, pb::first(cmdFields, F_DATA_UPLOAD).bytes);
    }
}

// --- команды после auth ---

void XiaomiChannel::sendCurrentTime()
{
    const QDateTime now = QDateTime::currentDateTime();

    pb::Writer date;
    date.varint(1, now.date().year());
    date.varint(2, now.date().month());
    date.varint(3, now.date().day());

    pb::Writer time;
    time.varint(1, now.time().hour());
    time.varint(2, now.time().minute());
    time.varint(3, now.time().second());
    time.varint(4, now.time().msec());

    const int offsetBlocks = now.offsetFromUtc() / 60 / 15;
    pb::Writer tz;
    tz.sint32(1, offsetBlocks);
    tz.sint32(2, 0);
    tz.str(3, QString::fromUtf8(QTimeZone::systemTimeZone().id()));

    pb::Writer clock;
    clock.msg(1, date);
    clock.msg(2, time);
    clock.msg(3, tz);
    clock.boolean(4, false);

    pb::Writer system;
    system.msg(F_CLOCK, clock);

    sendEncryptedCommand(buildCommand(TYPE_SYSTEM, 3, F_SYSTEM, system));
}

void XiaomiChannel::requestBattery()
{
    pb::Writer empty;
    sendEncryptedCommand(buildCommand(TYPE_SYSTEM, 1, -1, empty));
}

void XiaomiChannel::requestDeviceInfo()
{
    pb::Writer empty;
    sendEncryptedCommand(buildCommand(TYPE_SYSTEM, 2, -1, empty));
}

// --- health / activity ---

void XiaomiChannel::sendHealthCommand(quint32 subtype, const QByteArray &healthProto)
{
    pb::Writer cmd;
    cmd.varint(1, TYPE_HEALTH);
    cmd.varint(2, subtype);
    if (!healthProto.isEmpty())
        cmd.bytes(F_HEALTH, healthProto);
    sendEncryptedCommand(cmd.data);
}

void XiaomiChannel::sendNotification(const QString &appName, const QString &title,
                                     const QString &body, const QString &package)
{
    if (!m_authed)
        return;

    // Notification3
    pb::Writer n3;
    // package — идентификатор приложения-источника: по нему браслет
    // запрашивает иконку (ICON_QUERY) и группирует уведомления.
    // Свой id НЕ подставляем — иконка нашего приложения только для своих
    // уведомлений. Имя должно быть стабильным: браслет хранит иконки
    // по package, а хранилище крошечное (~6 слотов, чистится ребутом).
    n3.str(1, package.isEmpty() ? QStringLiteral("__unknown") : package);
    n3.str(2, appName.isEmpty() ? QStringLiteral("Aurora Fitness") : appName);
    n3.str(3, title);
    n3.str(4, QString());       // unknown4
    n3.str(5, body);
    n3.str(6, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd'T'HHmmss")));
    n3.varint(7, m_notificationId++);

    pb::Writer n2;
    n2.msg(F_NOTIFICATION3, n3);

    pb::Writer n1;
    n1.msg(F_NOTIFICATION2, n2);

    pb::Writer cmd;
    cmd.varint(1, TYPE_NOTIFICATION);
    cmd.varint(2, 0);           // CMD_NOTIFICATION_SEND
    cmd.msg(F_NOTIFICATION, n1);
    sendEncryptedCommand(cmd.data);
    qInfo() << "XiaomiChannel: уведомление отправлено:" << title;
}

// --- иконки уведомлений (Gadgetbridge XiaomiNotificationService) ---

void XiaomiChannel::handleNotification(quint32 subtype, const QByteArray &notificationProto)
{
    if (subtype == CMD_NOTIFICATION_ICON_QUERY) {
        const pb::Field query = pb::first(pb::parse(notificationProto), F_NOTIF_ICON_QUERY);
        if (!query.bytes.isEmpty())
            handleNotificationIconQuery(query.bytes);
    } else if (subtype == CMD_NOTIFICATION_ICON_REQUEST) {
        const pb::Field request = pb::first(pb::parse(notificationProto), F_NOTIF_ICON_REQUEST);
        if (!request.bytes.isEmpty())
            handleNotificationIconRequest(request.bytes);
    }
}

void XiaomiChannel::handleNotificationIconQuery(const QByteArray &iconPackageProto)
{
    // NotificationIconPackage{package=1}
    QString pkg = QString::fromUtf8(pb::first(pb::parse(iconPackageProto), 1).bytes);
    m_iconPackage = pkg.section(QLatin1Char('#'), 0, 0); // срезать соль старых схем
    qInfo() << "XiaomiChannel: браслет запросил иконку для" << pkg;

    // Хранилище иконок браслета ~6 слотов: отвечаем только если иконка
    // реально есть. Иначе молчим (как Gadgetbridge) — браслет покажет
    // свою иконку по умолчанию, а слот не расходуется.
    if (findIconPath(m_iconPackage).isEmpty()) {
        qInfo() << "XiaomiChannel: иконки для" << m_iconPackage << "нет — молчим";
        return;
    }

    // ICON_REPLY: subtype=15, Notification.notificationIconReply=14 (эхо package)
    pb::Writer iconPkg;
    iconPkg.str(1, pkg);

    pb::Writer n1;
    n1.msg(F_NOTIF_ICON_REPLY, iconPkg);

    sendEncryptedCommand(buildCommand(TYPE_NOTIFICATION, CMD_NOTIFICATION_ICON_REQUEST,
                                      F_NOTIFICATION, n1));
}

void XiaomiChannel::handleNotificationIconRequest(const QByteArray &iconRequestProto)
{
    // NotificationIconRequest{status=1, pixelFormat=2, size=3}
    const QList<pb::Field> fields = pb::parse(iconRequestProto);
    const quint32 status = quint32(pb::first(fields, 1).varint);
    const quint32 pixelFormat = quint32(pb::first(fields, 2).varint);
    const quint32 size = quint32(pb::first(fields, 3).varint);
    qInfo() << "XiaomiChannel: запрос иконки" << m_iconPackage
            << "- status" << status << "pixelFormat" << pixelFormat << "size" << size;

    if (status != 0)
        return;

    // Хранилище иконок браслета ограничено, дедупа по md5 нет — каждая
    // загрузка ест слот. Проверяем запросы 28 и 44: после успешных 28px
    // браслет продолжает просить 44px, а иконка не отображается. Грузим
    // каждый из размеров 28/44 один раз за сессию, 80px (25 КБ) — никогда.
    if (size > 44) {
        qInfo() << "XiaomiChannel: пропускаю размер иконки" << size << "(слишком большой)";
        return;
    }
    if (!m_iconUploading.isEmpty()) {
        qInfo() << "XiaomiChannel: иконка" << m_iconUploading << "ещё грузится — пропуск";
        return;
    }
    const QString servedKey = m_iconPackage + QStringLiteral(":") + QString::number(size);
    if (m_iconServed.contains(servedKey)) {
        qInfo() << "XiaomiChannel: иконка" << servedKey << "уже загружена — пропуск";
        return;
    }

    if (!m_uploader) {
        qWarning() << "XiaomiChannel: характеристика 0x0055 не найдена, иконку не отправить";
        return;
    }

    const QByteArray bitmap = buildIconBitmap(pixelFormat, size);
    if (bitmap.isEmpty())
        return;

    m_iconUploading = m_iconPackage; // переносится в m_iconServed по uploadFinished
    m_iconUploadingSize = int(size);
    m_uploader->startUpload(DataUpload::TYPE_NOTIFICATION_ICON, bitmap);
}

// Кандидаты путей иконки уведомления (по убыванию приоритета):
// hicolor по desktop-id → Icon= из desktop-файла (системные приложения OMP
// хранят иконки в теме) → маркеры __system (шестерёнка) / прочие (вопрос).
// Своя иконка подставляется только для своего пакета — она лежит в hicolor.
QStringList XiaomiChannel::iconCandidatePaths(const QString &pkg)
{
    if (!safeIconPackage(pkg))
        return QStringList();
    QStringList out;
    QString themeIcon;
    if (pkg == QLatin1String("__system")) {
        themeIcon = QStringLiteral("icon-m-setting");
    } else if (pkg.isEmpty() || pkg == QLatin1String("__unknown")) {
        themeIcon = QStringLiteral("icon-m-question");
    } else {
        for (const char *sz : {"128x128", "108x108", "86x86", "172x172"})
            out << QStringLiteral("/usr/share/icons/hicolor/%1/apps/%2.png")
                       .arg(QLatin1String(sz), pkg);
        QFile desktop(QStringLiteral("/usr/share/applications/") + pkg
                      + QStringLiteral(".desktop"));
        // У ru.omp.voicecall desktop-файла нет — иконка в voicecallui
        if (!desktop.exists())
            desktop.setFileName(QStringLiteral("/usr/share/applications/") + pkg
                                + QStringLiteral("ui.desktop"));
        if (desktop.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&desktop);
            in.setCodec("UTF-8");
            while (!in.atEnd()) {
                const QString line = in.readLine();
                if (line.startsWith(QLatin1String("Icon="))) {
                    themeIcon = line.mid(5).trimmed();
                    break;
                }
            }
        }
    }
    if (themeIcon.startsWith(QLatin1Char('/'))) {
        out << themeIcon;
    } else if (!themeIcon.isEmpty()) {
        for (const char *z : {"z2.0", "z1.75", "z1.5", "z1.25", "z1.0", "z0.75"})
            out << QStringLiteral("/usr/share/themes/aurora-default/meegotouch/%1/icons/%2.png")
                       .arg(QLatin1String(z), themeIcon);
    }
    return out;
}

QString XiaomiChannel::findIconPath(const QString &pkg) const
{
    if (!safeIconPackage(pkg))
        return QString();
    // Сначала кэш в общем конфиг-каталоге (его демон наполняет вне песочницы,
    // GUI-relay читает только оттуда), затем системные пути
    const QString cached = appConfigDir() + QStringLiteral("/icons/") + pkg
                           + QStringLiteral(".png");
    if (QFile::exists(cached))
        return cached;
    for (const QString &path : iconCandidatePaths(pkg)) {
        if (QFile::exists(path))
            return path;
    }
    return QString();
}

QByteArray XiaomiChannel::buildIconBitmap(quint32 pixelFormat, quint32 size) const
{
    if (size == 0 || size > 512) {
        qWarning() << "XiaomiChannel: некорректный размер иконки" << size;
        return QByteArray();
    }

    const QString iconPath = findIconPath(m_iconPackage);
    QImage img;
    if (!iconPath.isEmpty())
        img.load(iconPath);
    if (img.isNull()) {
        qWarning() << "XiaomiChannel: не удалось загрузить иконку для" << m_iconPackage;
        return QByteArray();
    }

    QImage scaled = img.convertToFormat(QImage::Format_ARGB32)
            .scaled(int(size), int(size), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QByteArray out;
    out.reserve(int(size * size * 4));
    for (int y = 0; y < int(size); ++y) {
        for (int x = 0; x < int(size); ++x) {
            const QRgb px = scaled.pixel(x, y);
            const int a = qAlpha(px), r = qRed(px), g = qGreen(px), b = qBlue(px);
            switch (pixelFormat) {
            case 0: // RGB_565_LE
            case 1: { // RGB_565_BE
                const quint16 v = quint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                if (pixelFormat == 0) {
                    out.append(char(v & 0xff));
                    out.append(char(v >> 8));
                } else {
                    out.append(char(v >> 8));
                    out.append(char(v & 0xff));
                }
                break;
            }
            case 2: // XRGB_8888_LE
            case 3: // ARGB_8888_LE — порядок байт B,G,R,A
                out.append(char(b));
                out.append(char(g));
                out.append(char(r));
                out.append(char(a));
                break;
            case 7: // ARGB_8565_LE — RGB565 LE + alpha
            case 8: { // ABGR_8565_LE — то же с переставленными R/B
                const int c1 = pixelFormat == 7 ? r : b;
                const int c2 = pixelFormat == 7 ? b : r;
                const quint16 v = quint16(((c1 >> 3) << 11) | ((g >> 2) << 5) | (c2 >> 3));
                out.append(char(v & 0xff));
                out.append(char(v >> 8));
                out.append(char(a));
                break;
            }
            default:
                qWarning() << "XiaomiChannel: неизвестный pixelFormat" << pixelFormat;
                return QByteArray();
            }
        }
    }
    return out;
}

// --- интерфейс для DataUpload ---

void XiaomiChannel::sendDataUploadCommand(const QByteArray &dataUploadProto)
{
    pb::Writer cmd;
    cmd.varint(1, TYPE_DATA_UPLOAD);
    cmd.varint(2, 0); // CMD_UPLOAD_START
    cmd.bytes(F_DATA_UPLOAD, dataUploadProto);
    sendEncryptedCommand(cmd.data);
}

QByteArray XiaomiChannel::encryptUploadPayload(const QByteArray &plain) const
{
    return xcrypto::aesCcmEncrypt(m_encryptionKey, makeNonce(m_encryptionNonce4, 0), plain);
}

void XiaomiChannel::writeUploadChar(const QByteArray &frame)
{
    if (!m_uploadPath.isEmpty())
        writeValue(m_uploadPath, frame);
}

int XiaomiChannel::uploadWriteSize() const
{
    if (!m_uploadPath.isEmpty()) {
        QDBusInterface props(QString::fromLatin1(BLUEZ), m_uploadPath,
                             QString::fromLatin1(IFACE_PROPS), QDBusConnection::systemBus());
        QDBusReply<QVariant> reply = props.call(QStringLiteral("Get"),
                                                QString::fromLatin1(IFACE_CHAR),
                                                QStringLiteral("MTU"));
        if (reply.isValid()) {
            const int mtu = reply.value().toInt();
            if (mtu >= 23)
                return mtu - 3; // минус заголовок ATT
        }
    }
    return 244; // как в Gadgetbridge: MTU 247 - 3
}

void XiaomiChannel::startActivityFetch()
{
    if (!m_authed) {
        emit authStatusChanged(QStringLiteral("Сначала нужна аутентификация"));
        return;
    }
    if (m_activityPath.isEmpty()) {
        emit authStatusChanged(QStringLiteral("Канал activity (0x0053) не найден"));
        return;
    }
    if (!m_fetcher) {
        m_fetcher = new ActivityFetcher(this);
        connect(m_fetcher, &ActivityFetcher::fileParsed,
                this, &XiaomiChannel::activityFileParsed);
        connect(m_fetcher, &ActivityFetcher::fetchProgress,
                this, &XiaomiChannel::activityFetchProgress);
        connect(m_fetcher, &ActivityFetcher::finished,
                this, &XiaomiChannel::activityFetchFinished);
    }
    m_fetcher->start();
}

void XiaomiChannel::handleSystem(const QByteArray &systemMsg)
{
    const QList<pb::Field> sysFields = pb::parse(systemMsg);

    const pb::Field power = pb::first(sysFields, F_POWER);
    if (!power.bytes.isEmpty()) {
        const pb::Field battery = pb::first(pb::parse(power.bytes), 1);
        if (!battery.bytes.isEmpty()) {
            const QList<pb::Field> bFields = pb::parse(battery.bytes);
            const int level = int(pb::first(bFields, 1).varint);
            const int state = int(pb::first(bFields, 2).varint);
            emit readyChanged();
            emit batteryReceived(level, state);
        }
    }

    const pb::Field deviceInfo = pb::first(sysFields, F_DEVICE_INFO);
    if (!deviceInfo.bytes.isEmpty()) {
        const QList<pb::Field> dFields = pb::parse(deviceInfo.bytes);
        emit deviceInfoReceived(QString::fromUtf8(pb::first(dFields, 1).bytes),
                                QString::fromUtf8(pb::first(dFields, 2).bytes),
                                QString::fromUtf8(pb::first(dFields, 4).bytes));
    }
}
