// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// lyriccrypt.cpp 的单元测试：纯函数（MD5 / AES-128-ECB / zlib·gzip 解压 / Base64）。
// 对应 docs/unit-test-plan.md 中「在线歌词模块」条目——
// KugouApi / NetEaseApi / LyricDownloader 依赖网络与签名，暂不测；
// LyricCrypt 是唯一可纯函数化的核心，最适合用已知向量 + 自洽性断言覆盖。
//
// 无任何网络依赖。运行方式：Debug 构建后执行 libdmusic-test（见 CMakeLists.txt）。

#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include <zlib.h>

#include "lyriccrypt.h"

namespace {

// 用 zlib C API 生成 zlib 流（首字节 0x78，触发 zlib 分支）
QByteArray makeZlib(const QByteArray &in)
{
    uLong bound = compressBound(static_cast<uLong>(in.size()));
    QByteArray out(static_cast<int>(bound), 0);
    uLongf outLen = bound;
    int ret = compress(reinterpret_cast<Bytef *>(out.data()), &outLen,
                       reinterpret_cast<const Bytef *>(in.constData()),
                       static_cast<uLong>(in.size()));
    if (ret != Z_OK) return QByteArray();
    out.resize(static_cast<int>(outLen));
    return out;
}

// 用 zlib C API 生成 gzip 流（首字节 0x1f，触发 gzip 分支）
QByteArray makeGzip(const QByteArray &in)
{
    z_stream strm = {};
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return QByteArray();
    QByteArray out;
    const int bufSize = 4096;
    QByteArray buf(bufSize, 0);
    strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    strm.avail_in = static_cast<uInt>(in.size());
    int ret = Z_OK;
    do {
        strm.next_out = reinterpret_cast<Bytef *>(buf.data());
        strm.avail_out = static_cast<uInt>(bufSize);
        ret = deflate(&strm, Z_FINISH);
        out.append(buf.constData(), bufSize - static_cast<int>(strm.avail_out));
    } while (ret == Z_OK);
    deflateEnd(&strm);
    if (ret != Z_STREAM_END) return QByteArray();
    return out;
}

// 16 字节 AES-128 密钥（LyricCrypt 内部用 EVP_aes_128_ecb）
const QByteArray kKey("0123456789abcdef");

} // namespace

// ============================================================================
// MD5
// ============================================================================
TEST(LyricCryptTest, md5Empty)
{
    EXPECT_EQ(LyricCrypt::md5(QByteArray()),
              QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"));
}

TEST(LyricCryptTest, md5Abc)
{
    EXPECT_EQ(LyricCrypt::md5(QByteArray("abc")),
              QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
}

TEST(LyricCryptTest, md5KnownSentence)
{
    EXPECT_EQ(LyricCrypt::md5(QByteArray("The quick brown fox jumps over the lazy dog")),
              QStringLiteral("9e107d9d372bb6826bd81d3542a419d6"));
}

TEST(LyricCryptTest, md5HexEqualsMd5)
{
    QByteArray data("deepin-music online lyric");
    EXPECT_EQ(LyricCrypt::md5Hex(data), LyricCrypt::md5(data));
}

TEST(LyricCryptTest, md5Deterministic)
{
    QByteArray data("hello world");
    EXPECT_EQ(LyricCrypt::md5(data), LyricCrypt::md5(data));
}

TEST(LyricCryptTest, md5LengthAlways32Hex)
{
    EXPECT_EQ(LyricCrypt::md5(QByteArray()).size(), 32);
    EXPECT_EQ(LyricCrypt::md5(QByteArray("x")).size(), 32);
    EXPECT_EQ(LyricCrypt::md5(QByteArray(1000, 'a')).size(), 32);
}

// ============================================================================
// AES-128-ECB（手动 PKCS#7 填充，OpenSSL 关闭自带 padding）
// ============================================================================
TEST(LyricCryptTest, aesEcbRoundTripEmpty)
{
    QByteArray plain;
    QByteArray cipher = LyricCrypt::aesEcbEncrypt(plain, kKey);
    EXPECT_EQ(cipher.size(), 16); // PKCS#7 把空明文填充成一个完整块
    EXPECT_EQ(LyricCrypt::aesEcbDecrypt(cipher, kKey), plain);
}

TEST(LyricCryptTest, aesEcbRoundTripShort)
{
    QByteArray plain("hi");
    QByteArray cipher = LyricCrypt::aesEcbEncrypt(plain, kKey);
    EXPECT_EQ(cipher.size(), 16); // 补齐到块边界
    EXPECT_EQ(LyricCrypt::aesEcbDecrypt(cipher, kKey), plain);
}

TEST(LyricCryptTest, aesEcbRoundTripExactBlock)
{
    QByteArray plain(16, 'A'); // 正好一个块
    QByteArray cipher = LyricCrypt::aesEcbEncrypt(plain, kKey);
    EXPECT_EQ(cipher.size(), 32); // 整块仍需额外一个填充块
    EXPECT_EQ(LyricCrypt::aesEcbDecrypt(cipher, kKey), plain);
}

TEST(LyricCryptTest, aesEcbRoundTripMultiBlock)
{
    QByteArray plain(40, 'Z'); // 2 块 + 8 字节
    QByteArray cipher = LyricCrypt::aesEcbEncrypt(plain, kKey);
    EXPECT_EQ(cipher.size() % 16, 0);
    EXPECT_EQ(LyricCrypt::aesEcbDecrypt(cipher, kKey), plain);
}

TEST(LyricCryptTest, aesEcbCiphertextDiffersFromPlaintext)
{
    QByteArray plain("visible plaintext");
    QByteArray cipher = LyricCrypt::aesEcbEncrypt(plain, kKey);
    EXPECT_NE(cipher, plain); // 非平凡密钥下 ECB 不得泄露明文
}

TEST(LyricCryptTest, aesEcbDeterministic)
{
    QByteArray plain("deterministic");
    EXPECT_EQ(LyricCrypt::aesEcbEncrypt(plain, kKey),
              LyricCrypt::aesEcbEncrypt(plain, kKey));
}

TEST(LyricCryptTest, aesEcbDifferentKeyDifferentCipher)
{
    QByteArray plain("same plaintext");
    QByteArray k2("fedcba9876543210");
    EXPECT_NE(LyricCrypt::aesEcbEncrypt(plain, kKey),
              LyricCrypt::aesEcbEncrypt(plain, k2));
}

// ============================================================================
// zlib / gzip 解压（zlibDecompress 自动识别 0x78 zlib / 0x1f gzip 头）
// ============================================================================
TEST(LyricCryptTest, zlibDecompressEmpty)
{
    EXPECT_TRUE(LyricCrypt::zlibDecompress(QByteArray()).isEmpty());
}

TEST(LyricCryptTest, zlibRoundTrip)
{
    QByteArray plain("hello zlib world, 你好世界");
    QByteArray comp = makeZlib(plain);
    ASSERT_FALSE(comp.isEmpty());
    EXPECT_EQ(static_cast<unsigned char>(comp.at(0)) & 0x0f, 0x08); // zlib 头
    QByteArray dec = LyricCrypt::zlibDecompress(comp);
    EXPECT_EQ(dec, plain);
}

TEST(LyricCryptTest, gzipRoundTrip)
{
    QByteArray plain("hello gzip world");
    QByteArray comp = makeGzip(plain);
    ASSERT_FALSE(comp.isEmpty());
    EXPECT_EQ(static_cast<unsigned char>(comp.at(0)), 0x1f); // gzip 魔数
    QByteArray dec = LyricCrypt::zlibDecompress(comp);
    EXPECT_EQ(dec, plain);
}

TEST(LyricCryptTest, zlibDecompressGarbageReturnsEmpty)
{
    // 非 zlib/gzip 的随机字节：inflate 失败应安全返回空，不崩溃
    QByteArray garbage(64, 'X');
    EXPECT_TRUE(LyricCrypt::zlibDecompress(garbage).isEmpty());
}

// ============================================================================
// Base64
// ============================================================================
TEST(LyricCryptTest, base64KnownVector)
{
    EXPECT_EQ(LyricCrypt::base64Encode(QByteArray("Man")),
              QStringLiteral("TWFu"));
}

TEST(LyricCryptTest, base64RoundTrip)
{
    QByteArray plain("deepin music 在线歌词");
    QString enc = LyricCrypt::base64Encode(plain);
    EXPECT_EQ(LyricCrypt::base64Decode(enc), plain);
}

TEST(LyricCryptTest, base64Empty)
{
    EXPECT_TRUE(LyricCrypt::base64Encode(QByteArray()).isEmpty());
    EXPECT_TRUE(LyricCrypt::base64Decode(QString()).isEmpty());
}
