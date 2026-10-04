// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 酷狗 KRC 容器解密（LyricCrypt::krcDecrypt）单元测试。纯函数、无网络依赖。
// 算法：跳 4 字节魔数头 + XOR(key) + zlib 解压。
// 测试用自定义 key 覆盖算法本身（不依赖真实 KG_KRC_KEY 白盒常量）。

#include <gtest/gtest.h>

#include <QByteArray>

#include <zlib.h>

#include "lyriccrypt.h"

namespace {

// 用 zlib C API 生成 zlib 流（首字节 0x78）
QByteArray zlibCompress(const QByteArray &in)
{
    uLong bound = compressBound(static_cast<uLong>(in.size()));
    QByteArray out(static_cast<int>(bound), 0);
    uLongf outLen = bound;
    if (compress(reinterpret_cast<Bytef *>(out.data()), &outLen,
                 reinterpret_cast<const Bytef *>(in.constData()),
                 static_cast<uLong>(in.size())) != Z_OK)
        return QByteArray();
    out.resize(static_cast<int>(outLen));
    return out;
}

// 与 krcDecrypt 内 XOR 一致的编码：返回 (zlib(plain) XOR key) 加上 4 字节头
QByteArray xorWithKey(const QByteArray &data, const QByteArray &key)
{
    QByteArray r(data.size(), 0);
    for (int i = 0; i < data.size(); ++i)
        r[i] = static_cast<char>(static_cast<unsigned char>(data.at(i)) ^
                                 static_cast<unsigned char>(key.at(i % key.size())));
    return r;
}

QByteArray makeKrc(const QByteArray &plain, const QByteArray &key,
                   const QByteArray &header = QByteArray(4, 0))
{
    return header + xorWithKey(zlibCompress(plain), key);
}

const QByteArray kKey("myTestKey12345");

} // namespace

TEST(KugouKrcTest, decryptEmptyReturnsEmpty)
{
    EXPECT_TRUE(LyricCrypt::krcDecrypt(QByteArray(), kKey).isEmpty());
}

TEST(KugouKrcTest, decryptTooShortReturnsEmpty)
{
    // 规则：size <= 4 直接返回空（不足魔数头）
    EXPECT_TRUE(LyricCrypt::krcDecrypt(QByteArray("abc"), kKey).isEmpty());
}

TEST(KugouKrcTest, roundTrip)
{
    QByteArray plain("hello kugou 你好世界 [00:01.00]");
    QByteArray enc = makeKrc(plain, kKey);
    EXPECT_EQ(LyricCrypt::krcDecrypt(enc, kKey), plain);
}

TEST(KugouKrcTest, skipsMagicHeader)
{
    // 前 4 字节魔数任意，解密时应被跳过（不计入内容）
    QByteArray plain("magic header skipped");
    QByteArray enc = makeKrc(plain, kKey, QByteArray("KRC1"));
    EXPECT_EQ(LyricCrypt::krcDecrypt(enc, kKey), plain);
}

TEST(KugouKrcTest, deterministic)
{
    QByteArray plain("deterministic lyrics");
    QByteArray a = makeKrc(plain, kKey);
    QByteArray b = makeKrc(plain, kKey);
    EXPECT_EQ(a, b);
    EXPECT_EQ(LyricCrypt::krcDecrypt(a, kKey), LyricCrypt::krcDecrypt(b, kKey));
}

TEST(KugouKrcTest, nonZlibBodyReturnsEmpty)
{
    // XOR 后的随机字节并非合法 zlib 流 -> 解压失败应安全返回空，不崩溃
    QByteArray garbage(64, 'X');
    QByteArray enc = QByteArray(4, 0) + xorWithKey(garbage, kKey);
    EXPECT_TRUE(LyricCrypt::krcDecrypt(enc, kKey).isEmpty());
}

TEST(KugouKrcTest, wrongKeyDoesNotRecoverPlaintext)
{
    QByteArray plain("secret lyrics");
    QByteArray enc = makeKrc(plain, kKey);
    EXPECT_NE(LyricCrypt::krcDecrypt(enc, QByteArray("wrongKeyWrongKe")), plain);
}
