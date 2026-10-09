// SPDX-License-Identifier: BSD-3-Clause

#include "crypto.h"

#include <openssl/evp.h>
#include <limits>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace xcrypto {

QByteArray randomBytes(int n)
{
    if (n <= 0)
        return QByteArray();
    QByteArray out(n, 0);
    if (RAND_bytes(reinterpret_cast<uchar *>(out.data()), n) != 1)
        return QByteArray();
    return out;
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg)
{
    QByteArray out(EVP_MAX_MD_SIZE, 0);
    unsigned int len = 0;
    if (!HMAC(EVP_sha256(),
         key.constData(), key.size(),
         reinterpret_cast<const uchar *>(msg.constData()), msg.size(),
         reinterpret_cast<uchar *>(out.data()), &len))
        return QByteArray();
    out.resize(int(len));
    return out;
}

QByteArray aesCcmEncrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &plain)
{
    if (key.size() != 16 || nonce.size() < 7 || nonce.size() > 13)
        return QByteArray();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return QByteArray();

    if (plain.size() > std::numeric_limits<int>::max() - 4) {
        EVP_CIPHER_CTX_free(ctx);
        return QByteArray();
    }
    QByteArray out(plain.size() + 4, 0);
    int len = 0;

    int finalLen = 0;
    const bool success = EVP_EncryptInit_ex(ctx, EVP_aes_128_ccm(), nullptr, nullptr, nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, nonce.size(), nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, 4, nullptr) == 1
            && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const uchar *>(key.constData()),
                                 reinterpret_cast<const uchar *>(nonce.constData())) == 1
            && EVP_EncryptUpdate(ctx, nullptr, &len, nullptr, plain.size()) == 1
            && EVP_EncryptUpdate(ctx, reinterpret_cast<uchar *>(out.data()), &len,
                                 reinterpret_cast<const uchar *>(plain.constData()), plain.size()) == 1
            && len == plain.size()
            && EVP_EncryptFinal_ex(ctx, reinterpret_cast<uchar *>(out.data()) + len, &finalLen) == 1
            && finalLen == 0
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_GET_TAG, 4, out.data() + plain.size()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return success ? out : QByteArray();
}

QByteArray aesCcmDecrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &cipherTag, bool *ok)
{
    if (ok)
        *ok = false;
    if (cipherTag.size() < 4)
        return QByteArray();

    const int cipherLen = cipherTag.size() - 4;
    if (key.size() != 16 || nonce.size() < 7 || nonce.size() > 13)
        return QByteArray();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return QByteArray();

    QByteArray out(cipherLen, 0);
    int len = 0;

    const bool success = EVP_DecryptInit_ex(ctx, EVP_aes_128_ccm(), nullptr, nullptr, nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, nonce.size(), nullptr) == 1
            && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, 4,
                                  const_cast<char *>(cipherTag.constData() + cipherLen)) == 1
            && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const uchar *>(key.constData()),
                                 reinterpret_cast<const uchar *>(nonce.constData())) == 1
            && EVP_DecryptUpdate(ctx, nullptr, &len, nullptr, cipherLen) == 1
            && EVP_DecryptUpdate(ctx, reinterpret_cast<uchar *>(out.data()), &len,
                                 reinterpret_cast<const uchar *>(cipherTag.constData()), cipherLen) == 1;
    EVP_CIPHER_CTX_free(ctx);

    if (!success || len != cipherLen)
        return QByteArray();
    out.resize(len);
    if (ok)
        *ok = true;
    return out;
}

} // namespace xcrypto
