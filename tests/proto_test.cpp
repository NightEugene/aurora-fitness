#include "app/src/xiaomi/proto.h"
#include "app/src/xiaomi/crypto.h"
#include "app/src/appsettings.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
    pb::Writer message;
    message.varint(1, std::numeric_limits<quint64>::max());
    message.bytes(2, QByteArray("hello"));
    message.sint32(3, std::numeric_limits<qint32>::min());
    message.sint32(4, -1);
    const auto fields = pb::parse(message.data);
    assert(fields.size() == 4);
    assert(fields[0].varint == std::numeric_limits<quint64>::max());
    assert(fields[1].bytes == "hello");
    assert(fields[2].varint == std::numeric_limits<quint32>::max());
    assert(fields[3].varint == 1);

    const char *bad[] = {
        "08",                           // отсутствует значение
        "0880",                         // незавершённый varint
        "0880808080808080808002",         // переполнение u64
        "12ffffffffffffffffff01",         // огромная длина
        "128080808010",                   // длина 2^32 (раньше сужалась до int)
        "12036162",                       // усечённое поле bytes
        "0900",                           // усечённое fixed64
        "0d00",                           // усечённое fixed32
        "00",                             // номер поля 0
        "808080801001",                   // номер поля вне proto2-диапазона
        "08011203",                       // не принимать корректный префикс битого сообщения
    };
    for (const char *hex : bad)
        assert(pb::parse(QByteArray::fromHex(hex)).isEmpty());

    // Все усечённые варианты длинного varint должны отвергаться.
    pb::Writer large;
    large.varint(1, std::numeric_limits<quint64>::max());
    for (int i = 1; i < large.data.size(); ++i)
        assert(pb::parse(large.data.left(i)).isEmpty());
    assert(safeIconPackage(QStringLiteral("ru.nighteugene.aurorafitness")));
    assert(safeIconPackage(QStringLiteral("__system")));
    assert(!safeIconPackage(QStringLiteral("../other")));
    assert(!safeIconPackage(QStringLiteral("..")));
    assert(!safeIconPackage(QStringLiteral("other\\path")));

    const QByteArray key(16, 'k'), nonce(12, 'n'), plain("test command");
    const QByteArray encrypted = xcrypto::aesCcmEncrypt(key, nonce, plain);
    assert(encrypted.size() == plain.size() + 4);
    bool ok = false;
    assert(xcrypto::aesCcmDecrypt(key, nonce, encrypted, &ok) == plain && ok);
    QByteArray tampered = encrypted;
    tampered[0] = char(tampered.at(0) ^ 1);
    assert(xcrypto::aesCcmDecrypt(key, nonce, tampered, &ok).isEmpty() && !ok);
    assert(xcrypto::aesCcmEncrypt(QByteArray(), nonce, plain).isEmpty());
    assert(xcrypto::aesCcmDecrypt(key, QByteArray(), encrypted, &ok).isEmpty() && !ok);
    assert(xcrypto::randomBytes(0).isEmpty());
    assert(xcrypto::randomBytes(16).size() == 16);
    std::puts("crypto and icon paths: regression checks passed");
    std::puts("proto: valid messages and malformed-input regression checks passed");
}
