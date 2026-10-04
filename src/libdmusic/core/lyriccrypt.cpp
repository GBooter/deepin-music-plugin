// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lyriccrypt.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <zlib.h>

extern "C" {
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/md5.h>
}

namespace LyricCrypt {

static QByteArray pkcs7Pad(const QByteArray &data, int blockSize)
{
    int padLen = blockSize - (data.size() % blockSize);
    QByteArray padded = data;
    padded.append(QByteArray(padLen, static_cast<char>(padLen)));
    return padded;
}

static QByteArray pkcs7Unpad(const QByteArray &data)
{
    if (data.isEmpty()) return data;
    int padLen = static_cast<unsigned char>(data.at(data.size() - 1));
    if (padLen < 1 || padLen > AES_BLOCK_SIZE) return data;
    return data.left(data.size() - padLen);
}

QString md5(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString md5Hex(const QByteArray &data)
{
    return md5(data);
}

QByteArray aesEcbEncrypt(const QByteArray &plain, const QByteArray &key)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr,
                       reinterpret_cast<const unsigned char *>(key.constData()), nullptr);
    EVP_CIPHER_CTX_set_padding(ctx, 0); // we do PKCS#7 ourselves

    QByteArray padded = pkcs7Pad(plain, AES_BLOCK_SIZE);
    QByteArray out(padded.size() + AES_BLOCK_SIZE, 0);
    int outLen = 0;
    int totalLen = 0;

    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()),
                       &outLen,
                       reinterpret_cast<const unsigned char *>(padded.constData()),
                       padded.size());
    totalLen = outLen;

    EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + totalLen, &outLen);
    totalLen += outLen;

    EVP_CIPHER_CTX_free(ctx);
    out.resize(totalLen);
    return out;
}

QByteArray aesEcbDecrypt(const QByteArray &cipher, const QByteArray &key)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr,
                       reinterpret_cast<const unsigned char *>(key.constData()), nullptr);
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    QByteArray out(cipher.size() + AES_BLOCK_SIZE, 0);
    int outLen = 0;
    int totalLen = 0;

    EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()),
                       &outLen,
                       reinterpret_cast<const unsigned char *>(cipher.constData()),
                       cipher.size());
    totalLen = outLen;

    EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + totalLen, &outLen);
    totalLen += outLen;

    EVP_CIPHER_CTX_free(ctx);
    out.resize(totalLen);
    return pkcs7Unpad(out);
}

QByteArray zlibDecompress(const QByteArray &data)
{
    if (data.isEmpty()) return QByteArray();

    z_stream strm = {};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // auto-detect: if first byte is 0x78 (zlib header) or 0x1f (gzip header)
    int windowBits = 15;
    unsigned char first = static_cast<unsigned char>(data.at(0));
    if (first == 0x1f) {
        windowBits += 16; // gzip
    } else if ((first & 0x0f) == 0x08) {
        // zlib header
    }

    int ret = inflateInit2(&strm, windowBits);
    if (ret != Z_OK) return QByteArray();

    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    strm.avail_in = data.size();

    QByteArray result;
    char buf[4096];
    do {
        strm.next_out = reinterpret_cast<Bytef *>(buf);
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            return QByteArray();
        }
        result.append(buf, sizeof(buf) - strm.avail_out);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return result;
}

QByteArray base64Decode(const QString &str)
{
    return QByteArray::fromBase64(str.toUtf8());
}

QString base64Encode(const QByteArray &data)
{
    return QString::fromLatin1(data.toBase64());
}

QByteArray krcDecrypt(const QByteArray &encrypted, const QByteArray &key)
{
    if (encrypted.size() <= 4) return QByteArray();
    // 跳过 4 字节魔数头
    QByteArray data = encrypted.mid(4);
    // XOR 解密
    QByteArray decrypted(data.size(), 0);
    for (int i = 0; i < data.size(); ++i)
        decrypted[i] = static_cast<char>(
            static_cast<unsigned char>(data.at(i)) ^
            static_cast<unsigned char>(key.at(i % key.size())));
    // zlib 解压（自动识别 zlib/gzip 头）
    return zlibDecompress(decrypted);
}

// 网易云 EAPI 协议常量（与 NetEaseApi 中保持一致）
static const QByteArray EAPI_SIGN_PFX = "nobody";
static const QByteArray EAPI_SIGN_MID = "use";
static const QByteArray EAPI_SIGN_SFX = "md5forencrypt";
static const QByteArray EAPI_SEP = "-36cd479b6b5-";

QByteArray eapiEncrypt(const QByteArray &path, const QVariantMap &params, const QByteArray &key)
{
    QByteArray pBytes = QJsonDocument(QJsonObject::fromVariantMap(params)).toJson(QJsonDocument::Compact);
    QByteArray signSrc = EAPI_SIGN_PFX + path + EAPI_SIGN_MID + pBytes + EAPI_SIGN_SFX;
    QByteArray sign = md5(signSrc).toLatin1();
    QByteArray aesSrc = path + EAPI_SEP + pBytes + EAPI_SEP + sign;
    return aesEcbEncrypt(aesSrc, key);
}

QByteArray eapiDecrypt(const QByteArray &data, const QByteArray &key)
{
    return aesEcbDecrypt(data, key);
}

} // namespace LyricCrypt
