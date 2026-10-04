// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 在线歌词「运行时联网验证」工具（非单元测试）。
//
// 目的：单元测试用 mock 覆盖了编排逻辑，但无法证明**真实网络**下
//   Kugou 搜索 → KRC 解密 → KRC→标准 LRC 转换 → 落盘
// 这条真实链路可用。本工具直接驱动生产类 `LyricDownloader`（内部使用真实
// KugouApi / NetEaseApi / HttpClient）联网跑一遍，把结果打到 stdout。
//
// 与单测的分工：
//   单测（tests/libdmusic-test）—— 离线、可重复、覆盖回退顺序与错误判定。
//   本工具 —— 真实联网、覆盖「服务端是否真的能用」，适合手工冒烟 / 接入 CI 前的抽查。
//
// 只依赖 QCoreApplication，**不需要桌面环境 / 显示服务**。
//
// 用法：
//   lyric_runtime_check                 # 用内置样例歌曲验证
//   lyric_runtime_check <title> [artist] # 验证指定歌曲
//   lyric_runtime_check --list          # 列出内置样例
//
// 退出码：0=至少一首歌成功取到歌词；1=全部失败（网络不通 / 服务端变更 / 匹配失败）。

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include <QElapsedTimer>
#include <cstdio>
#include <unistd.h>

#include "global.h"
#include "lyricdownloader.h"

namespace {

struct Probe {
    QString title;
    QString artist;
};

// 内置冒烟样例：覆盖中文、英文、以及"逐字(Karaoke)"歌词（Kugou 逐字质量最好）。
QList<Probe> defaultProbes()
{
    return {
        {QStringLiteral("晴天"),   QStringLiteral("周杰伦")},
        {QStringLiteral("Yesterday"), QStringLiteral("The Beatles")},
    };
}

int runOne(LyricDownloader &dl, const Probe &p, const QString &outDir)
{
    DMusic::MediaMeta m;
    m.title = p.title;
    m.artist = p.artist;
    // localPath 必须非空（downloadAndSaveLyrics 的前置校验），这里给一个不存在的占位路径即可
    m.localPath = QDir::temp().filePath(QStringLiteral("%1 - %2.mp3").arg(p.artist, p.title));
    m.hash = QStringLiteral("%1_%2").arg(p.artist, p.title);

    const QString savePath = outDir + QLatin1Char('/') + m.hash + QStringLiteral(".lrc");
    QString savedPath;

    QElapsedTimer t;
    t.start();
    // 真实同步网络调用（内部 QEventLoop 驱动），当前线程阻塞直到完成
    const QString lyrics = dl.downloadAndSaveLyrics(m, savePath, &savedPath);
    const qint64 ms = t.elapsed();

    const bool got = !lyrics.isEmpty();
    const bool netErr = dl.networkError();

    printf("---- [%s - %s] ----\n", qPrintable(p.artist), qPrintable(p.title));
    printf("  result   : %s (%lld ms)\n", got ? "LYRICS_OK" : "LYRICS_FAIL", (long long)ms);
    printf("  netError : %s\n", netErr ? "true" : "false");
    printf("  savedTo  : %s\n", savedPath.isEmpty() ? "<none>" : qPrintable(savedPath));
    if (got) {
        const QStringList lines = lyrics.split(QLatin1Char('\n'));
        int wordTimed = 0;
        for (const QString &l : lines)
            if (l.count(QLatin1Char('[')) > 1) ++wordTimed;   // 多个时间戳 → 逐字行
        printf("  chars    : %d   lines: %d   wordByWordLines: %d\n",
               (int)lyrics.size(), (int)lines.size(), wordTimed);
        for (int i = 0; i < lines.size() && i < 2; ++i) {
            const QString s = lines.at(i).trimmed();
            if (!s.isEmpty()) { printf("  | %s\n", qPrintable(s.left(120))); break; }
        }
        // 落盘校验：确认真实写出了文件且非空
        if (!savedPath.isEmpty()) {
            QFile f(savedPath);
            const bool ok = f.exists() && f.size() > 0;
            printf("  fileCheck: %s (%lld bytes on disk)\n",
                   ok ? "OK" : "MISSING/EMPTY", (long long)(ok ? f.size() : 0));
        }
    }
    printf("\n");
    fflush(stdout);
    return got ? 1 : 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("deepin-music"));
    QCoreApplication::setOrganizationName(QStringLiteral("deepin"));

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--list"))) {
        for (const Probe &p : defaultProbes())
            printf("%s - %s\n", qPrintable(p.artist), qPrintable(p.title));
        return 0;
    }

    // 缓存/输出目录：放在可写临时位置。无桌面环境下 DmGlobal::init() 未跑，
    // cachePath() 默认为空会让缓存回退路径退化成不可写的 /lyrics，故显式设置。
    const QString outDir = QDir::temp().filePath(QStringLiteral("lyric_runtime_out"));
    QDir().mkpath(outDir);
    DmGlobal::setCachePath(outDir);

    // 硬性兜底超时：任一源卡死时也能退出（单源 httpGet 已有 15s×3 重试）
    QTimer::singleShot(180000, []() {
        fprintf(stderr, "[FATAL] 超过 180s 未完成，强制退出\n");
        ::_exit(2);
    });

    QList<Probe> probes;
    if (args.size() >= 3) {                       // lyric_runtime_check <title> <artist>
        probes.append({args.at(1), args.at(2)});
    } else {
        probes = defaultProbes();
    }

    LyricDownloader dl;   // 生产路径：内部构造真实 KugouApi / NetEaseApi / HttpClient

    int okCount = 0;
    for (const Probe &p : probes)
        okCount += runOne(dl, p, outDir);

    printf("SUMMARY: %d/%d 首歌成功取到歌词\n", okCount, (int)probes.size());
    return okCount > 0 ? 0 : 1;
}
