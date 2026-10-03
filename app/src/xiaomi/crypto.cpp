// SPDX-License-Identifier: BSD-3-Clause

#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace xcrypto {

QByteArray randomBytes(int n)
{
    QByteArray out(n, 0);
    RAND_bytes(reinterpret_cast<uchar *>(out.data()), n);
    return out;
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &msg)
{
    QByteArray out(EVP_MAX_MD_SIZE, 0);
    unsigned int len = 0;
    HMAC(EVP_sha256(),
         key.constData(), key.size(),
         reinterpret_cast<const uchar *>(msg.constData()), msg.size(),
         reinterpret_cast<uchar *>(out.data()), &len);
    out.resize(int(len));
    return out;
}

QByteArray aesCcmEncrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &plain)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return QByteArray();

    QByteArray out(plain.size() + 4, 0);
    int len = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_128_ccm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, nonce.size(), nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, 4, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                       reinterpret_cast<const uchar *>(key.constData()),
                       reinterpret_cast<const uchar *>(nonce.constData()));
    // Для CCM длину plaintext нужно объявить заранее
    EVP_EncryptUpdate(ctx, nullptr, &len, nullptr, plain.size());
    EVP_EncryptUpdate(ctx, reinterpret_cast<uchar *>(out.data()), &len,
                      reinterpret_cast<const uchar *>(plain.constData()), plain.size());
    EVP_EncryptFinal_ex(ctx, reinterpret_cast<uchar *>(out.data()) + len, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_GET_TAG, 4, out.data() + plain.size());
    EVP_CIPHER_CTX_free(ctx);
    return out;
}

QByteArray aesCcmDecrypt(const QByteArray &key, const QByteArray &nonce, const QByteArray &cipherTag, bool *ok)
{
    if (ok)
        *ok = false;
    if (cipherTag.size() < 4)
        return QByteArray();

    const int cipherLen = cipherTag.size() - 4;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return QByteArray();

    QByteArray out(cipherLen, 0);
    int len = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_128_ccm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_IVLEN, nonce.size(), nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_CCM_SET_TAG, 4,
                        const_cast<char *>(cipherTag.constData() + cipherLen));
    EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                       reinterpret_cast<const uchar *>(key.constData()),
                       reinterpret_cast<const uchar *>(nonce.constData()));
    EVP_DecryptUpdate(ctx, nullptr, &len, nullptr, cipherLen);
    const int rv = EVP_DecryptUpdate(ctx, reinterpret_cast<uchar *>(out.data()), &len,
                                     reinterpret_cast<const uchar *>(cipherTag.constData()),
                                     cipherLen);
    EVP_CIPHER_CTX_free(ctx);

    if (rv <= 0)
        return QByteArray();
    out.resize(len);
    if (ok)
        *ok = true;
    return out;
}

} // namespace xcrypto
