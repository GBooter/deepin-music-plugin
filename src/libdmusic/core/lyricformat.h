// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICFORMAT_H
#define LYRICFORMAT_H

#include <QString>
#include <QStringList>
#include <QRegularExpression>

namespace LyricFormat {

/**
 * 将毫秒转换为标准 LRC 时间戳格式 mm:ss.xx
 *
 * 原本在 lyricapi_kg.cpp 与 lyricapi_ne.cpp 中各有一份重复实现，
 * 提取到此处供两者共用，避免修改时漏改一处导致行为不一致。
 *
 * @param ms 毫秒数
 * @return 形如 "03:25.12" 的时间戳；超过 99 分钟时分钟数自然溢出为两位以上
 */
inline QString msToLrcTime(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 totalSeconds = ms / 1000;
    const qint64 minutes = totalSeconds / 60;
    const qint64 seconds = totalSeconds % 60;
    const qint64 centiseconds = (ms % 1000) / 10;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

/**
 * 将酷狗 KRC 逐字格式转换为标准 LRC 格式。
 *
 * KRC 行格式: [line_start,line_duration]<word_start,word_duration,density>word_content
 * 标准格式:   [mm:ss.xx]word1[mm:ss.xx]word2...
 * 无逐字信息时退化为 [mm:ss.xx]整行内容。
 *
 * 从 lyricapi_kg.cpp 的 convertKrcToStandardLrc 提取而来，与 yrcToLrc 共用此命名空间，
 * 避免 kg / ne 两份近重复实现修改时漏改一处。纯函数、无网络依赖。
 */
inline QString krcToLrc(const QString &krcText)
{
    if (krcText.isEmpty()) return QString();

    QStringList lines = krcText.split('\n');
    QString result;

    QRegularExpression lineRegex("^\\[(\\d+),(\\d+)\\](.*)$");
    QRegularExpression wordRegex("<(\\d+),(\\d+),\\d+>([^\\<]*)");

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || !trimmed.startsWith('[')) continue;

        QRegularExpressionMatch lineMatch = lineRegex.match(trimmed);
        if (!lineMatch.hasMatch()) continue;

        qint64 lineStart = lineMatch.captured(1).toLongLong();
        QString content = lineMatch.captured(3);

        QString lrcLine = "[" + msToLrcTime(lineStart) + "]";
        bool hasWords = false;
        QRegularExpressionMatchIterator wordIt = wordRegex.globalMatch(content);

        while (wordIt.hasNext()) {
            QRegularExpressionMatch wordMatch = wordIt.next();
            qint64 wordStart = lineStart + wordMatch.captured(1).toLongLong();
            QString wordText = wordMatch.captured(3);

            if (!wordText.isEmpty()) {
                lrcLine += "[" + msToLrcTime(wordStart) + "]" + wordText;
                hasWords = true;
            }
        }

        if (hasWords) {
            if (!result.isEmpty()) result += '\n';
            result += lrcLine;
        } else {
            if (!result.isEmpty()) result += '\n';
            result += "[" + msToLrcTime(lineStart) + "]" + content;
        }
    }

    return result;
}

/**
 * 将网易云 YRC 逐字格式转换为标准 LRC 格式。
 *
 * YRC 行格式: [line_start,line_duration](word_start,word_duration,density)word_content
 * 与 KRC 仅逐字包裹符不同（KRC 用 <>，YRC 用 ()），转换逻辑一致。
 *
 * 从 lyricapi_ne.cpp 的 convertYrcToStandardLrc 提取而来。纯函数、无网络依赖。
 */
inline QString yrcToLrc(const QString &yrcText)
{
    if (yrcText.isEmpty()) return QString();

    QStringList lines = yrcText.split('\n');
    QString result;

    QRegularExpression lineRegex("^\\[(\\d+),(\\d+)\\](.*)$");
    QRegularExpression wordRegex("\\((\\d+),(\\d+),\\d+\\)([^\\(]*)");

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || !trimmed.startsWith('[')) continue;

        QRegularExpressionMatch lineMatch = lineRegex.match(trimmed);
        if (!lineMatch.hasMatch()) continue;

        qint64 lineStart = lineMatch.captured(1).toLongLong();
        QString content = lineMatch.captured(3);

        QString lrcLine = "[" + msToLrcTime(lineStart) + "]";
        bool hasWords = false;
        QRegularExpressionMatchIterator wordIt = wordRegex.globalMatch(content);

        while (wordIt.hasNext()) {
            QRegularExpressionMatch wordMatch = wordIt.next();
            qint64 wordStart = lineStart + wordMatch.captured(1).toLongLong();
            QString wordText = wordMatch.captured(3);

            if (!wordText.isEmpty()) {
                lrcLine += "[" + msToLrcTime(wordStart) + "]" + wordText;
                hasWords = true;
            }
        }

        if (hasWords) {
            if (!result.isEmpty()) result += '\n';
            result += lrcLine;
        } else {
            if (!result.isEmpty()) result += '\n';
            result += "[" + msToLrcTime(lineStart) + "]" + content;
        }
    }

    return result;
}

} // namespace LyricFormat

#endif // LYRICFORMAT_H
