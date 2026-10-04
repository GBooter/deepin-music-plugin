// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// KRC / YRC 逐字格式 → 标准 LRC 转换的单元测试（纯函数，无网络依赖）。
// 对应 docs/unit-test-plan.md 中「在线歌词模块」条目——
// KugouApi / NetEaseApi 的逐字转换逻辑已提取到 LyricFormat::krcToLrc / yrcToLrc
// （见 src/libdmusic/core/lyricformat.h），供两者共用，此处直接覆盖。

#include <gtest/gtest.h>

#include <QString>

#include "lyricformat.h"

// ============================================================================
// KRC → LRC
// ============================================================================
TEST(LyricFormatKrcTest, empty)
{
    EXPECT_TRUE(LyricFormat::krcToLrc(QString()).isEmpty());
}

TEST(LyricFormatKrcTest, karaokeLine)
{
    // KRC 逐字行: [line_start,line_dur]<word_start,word_dur,density>word ...
    QString input("[1000,2000]<0,500,0>hello<500,500,0>world");
    // line@1000ms -> 00:01.00; word1@1000 -> 00:01.00; word2@1500 -> 00:01.50
    EXPECT_EQ(LyricFormat::krcToLrc(input),
              QString("[00:01.00][00:01.00]hello[00:01.50]world"));
}

TEST(LyricFormatKrcTest, plainLineNoWords)
{
    // 无逐字信息时退化为整行 [mm:ss.xx]content
    QString input("[1000,2000]plain line");
    EXPECT_EQ(LyricFormat::krcToLrc(input), QString("[00:01.00]plain line"));
}

TEST(LyricFormatKrcTest, multiLineSkipsNonKrc)
{
    // 非 KRC 行（不以 [ 开头）、空行应被跳过
    QString input("garbage line\n[1000,2000]first\n\n[2000,1000]second");
    EXPECT_EQ(LyricFormat::krcToLrc(input),
              QString("[00:01.00]first\n[00:02.00]second"));
}

TEST(LyricFormatKrcTest, malformedLineSkipped)
{
    // 行首正则不匹配（非 [数字,数字]）时跳过该行
    QString input("[notvalid]xyz\n[1000,2000]ok");
    EXPECT_EQ(LyricFormat::krcToLrc(input), QString("[00:01.00]ok"));
}

// ============================================================================
// YRC → LRC（与 KRC 仅逐字包裹符不同：<> vs ()）
// ============================================================================
TEST(LyricFormatYrcTest, empty)
{
    EXPECT_TRUE(LyricFormat::yrcToLrc(QString()).isEmpty());
}

TEST(LyricFormatYrcTest, karaokeLine)
{
    QString input("[1000,2000](0,500,0)hello(500,500,0)world");
    EXPECT_EQ(LyricFormat::yrcToLrc(input),
              QString("[00:01.00][00:01.00]hello[00:01.50]world"));
}

TEST(LyricFormatYrcTest, plainLineNoWords)
{
    QString input("[1000,2000]plain line");
    EXPECT_EQ(LyricFormat::yrcToLrc(input), QString("[00:01.00]plain line"));
}

TEST(LyricFormatYrcTest, krcAndYrcEquivalentForSameShape)
{
    // 同一形状（仅包裹符不同）应得到完全相同的输出
    QString krc("[1000,2000]<0,500,0>a<500,500,0>b");
    QString yrc("[1000,2000](0,500,0)a(500,500,0)b");
    EXPECT_EQ(LyricFormat::krcToLrc(krc), LyricFormat::yrcToLrc(yrc));
}
