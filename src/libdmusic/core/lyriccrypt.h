// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICCRYPT_H
#define LYRICCRYPT_H

#include <QString>
#include <QByteArray>
#include <QVariantMap>

namespace LyricCrypt {

// MD5 hash -> hex string
QString md5(const QByteArray &data);
QString md5Hex(const QByteArray &data);

// AES-ECB with PKCS#7 padding (OpenSSL)
QByteArray aesEcbEncrypt(const QByteArray &plain, const QByteArray &key);
QByteArray aesEcbDecrypt(const QByteArray &cipher, const QByteArray &key);

// zlib decompress (auto-detect gzip/zlib header)
QByteArray zlibDecompress(const QByteArray &data);

// Base64
QByteArray base64Decode(const QString &str);
QString base64Encode(const QByteArray &data);

// KRC 容器解密（酷狗）：跳 4 字节魔数头 + XOR(key) + zlib 解压。纯函数、无网络依赖。
// KugouApi::krcDecrypt 转发至此，便于单元测试直接覆盖核心算法。
QByteArray krcDecrypt(const QByteArray &encrypted, const QByteArray &key);

// EAPI 请求加密 / 响应解密（网易云）：签名拼装 + AES-128-ECB（固定密钥，无随机盐）。
// 纯函数、无网络依赖。NetEaseApi::eapiEncrypt/eapiDecrypt 转发至此。
QByteArray eapiEncrypt(const QByteArray &path, const QVariantMap &params, const QByteArray &key);
QByteArray eapiDecrypt(const QByteArray &data, const QByteArray &key);

} // namespace LyricCrypt

#endif // LYRICCRYPT_H
