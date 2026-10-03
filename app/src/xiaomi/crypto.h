// SPDX-License-Identifier: BSD-3-Clause

#ifndef XIAOMI_CRYPTO_H
#define XIAOMI_CRYPTO_H

#include <QByteArray>

// Обёртки над OpenSSL (libcrypto) для протокола Xiaomi.
namespace xcrypto {

QByteArray randomBytes(int n);
QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg);

// AES-128-CCM, nonce 12 байт, тег 4 байта. Шифрование: результат = cipher || tag.
QByteArray aesCcmEncrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &plain);
// При ошибке (в т.ч. несовпадении тега) вернёт пустой массив и ok=false.
QByteArray aesCcmDecrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &cipherTag, bool *ok);

} // namespace xcrypto

#endif // XIAOMI_CRYPTO_H
