// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 网易云 EAPI 请求加密 / 响应解密（LyricCrypt::eapiEncrypt/eapiDecrypt）单元测试。
// 纯函数、无网络依赖。算法：签名拼装（nobody+path+use+params+md5forencrypt 的 MD5）
// + AES-128-ECB（固定密钥，无随机盐）。

#include <gtest/gtest.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "lyriccrypt.h"

namespace {
QByteArray compactJson(const QVariantMap &params)
{
    return QJsonDocument(QJsonObject::fromVariantMap(params)).toJson(QJsonDocument::Compact);
}
} // namespace

TEST(NetEaseEapiTest, roundTripIntegrity)
{
    QVariantMap params;
    params["id"] = "123456";
    params["lv"] = "-1";
    params["tv"] = "-1";
    QByteArray path("/eapi/song/lyric/v1");
    QByteArray key("e82ckenh8dichen8");

    QByteArray cipher = LyricCrypt::eapiEncrypt(path, params, key);
    ASSERT_FALSE(cipher.isEmpty());

    QByteArray dec = LyricCrypt::eapiDecrypt(cipher, key);
    // 解密后应完整包含 path 与参数的 compact JSON
    EXPECT_TRUE(dec.startsWith(path));
    EXPECT_TRUE(dec.contains(compactJson(params)));
}

TEST(NetEaseEapiTest, signCorrectness)
{
    QVariantMap params;
    params["id"] = "123456";
    QByteArray path("/eapi/song/lyric/v1");
    QByteArray key("e82ckenh8dichen8");

    QByteArray pBytes = compactJson(params);
    QByteArray expectedSign = LyricCrypt::md5(QByteArray("nobody") + path +
                                             QByteArray("use") + pBytes +
                                             QByteArray("md5forencrypt")).toLatin1();

    QByteArray dec = LyricCrypt::eapiDecrypt(
        LyricCrypt::eapiEncrypt(path, params, key), key);
    // 解密结果必须包含按协议拼装的签名串，验证签名分支正确
    EXPECT_TRUE(dec.contains(expectedSign));
}

TEST(NetEaseEapiTest, deterministic)
{
    QVariantMap params;
    params["id"] = "1";
    QByteArray path("/eapi/x");
    QByteArray key("e82ckenh8dichen8");
    EXPECT_EQ(LyricCrypt::eapiEncrypt(path, params, key),
              LyricCrypt::eapiEncrypt(path, params, key));
}

TEST(NetEaseEapiTest, differentPathDifferentCipher)
{
    QVariantMap params;
    params["id"] = "1";
    QByteArray key("e82ckenh8dichen8");
    EXPECT_NE(LyricCrypt::eapiEncrypt("/eapi/a", params, key),
              LyricCrypt::eapiEncrypt("/eapi/b", params, key));
}

TEST(NetEaseEapiTest, differentParamsDifferentCipher)
{
    QVariantMap p1;
    p1["id"] = "1";
    QVariantMap p2;
    p2["id"] = "2";
    QByteArray path("/eapi/x");
    QByteArray key("e82ckenh8dichen8");
    EXPECT_NE(LyricCrypt::eapiEncrypt(path, p1, key),
              LyricCrypt::eapiEncrypt(path, p2, key));
}

TEST(NetEaseEapiTest, ciphertextNotPlaintext)
{
    QVariantMap params;
    params["id"] = "1";
    QByteArray path("/eapi/song/lyric/v1");
    QByteArray key("e82ckenh8dichen8");
    QByteArray cipher = LyricCrypt::eapiEncrypt(path, params, key);
    // 非平凡密钥下密文不得泄露可读明文
    EXPECT_NE(cipher, path + QByteArray("-36cd479b6b5-") + compactJson(params));
}
