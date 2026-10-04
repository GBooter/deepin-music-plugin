// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICDOWNLOADER_H
#define LYRICDOWNLOADER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVariantList>

#include "global.h"

class NetEaseApi;
class KugouApi;

// 网络层抽象：把"同步 HTTP GET"从 LyricDownloader 剥离开，便于单元测试注入 mock，
// 隔离对真实网络的依赖。无 Q_OBJECT，测试 TU 可直接包含，不会触发 AUTOMOC 冲突。
class IHttpClient
{
public:
    virtual ~IHttpClient() = default;

    // 同步 GET。成功返回响应体；失败（超时 / 非 429 的 HTTP 错误）返回空字节数组。
    // 429 限流由实现内部自动重试，对调用方透明。
    virtual QByteArray get(const QString &url) = 0;

    // 最近一次 get() 是否因网络/HTTP 错误失败（不含 429 限流重试）。
    // 调用方据此区分"没找到歌词"与"网络错误"。
    virtual bool networkError() const = 0;
};

struct LyricSearchResult {
    QString title;
    QString artist;
    QString album;
    QString source;    // "NetEase", "Kugou" or "LRCLIB"
    qint64 id = 0;     // NetEase song ID
    qint64 duration = 0;
    // LRCLIB fields
    QString lrclibTrackName;
    QString lrclibArtistName;
    QString lrclibAlbumName;
    // Kugou fields
    QString kugouHash;
};

class LyricDownloader : public QObject
{
    Q_OBJECT
public:
    explicit LyricDownloader(QObject *parent = nullptr);
    // 注入式构造：用于单元测试。调用方需保证 neApi/kgApi/http 在 downloader
    // 生命周期内有效，且 downloader 不接管其所有权（默认构造才会 new 并 parent 到自身）。
    LyricDownloader(NetEaseApi *neApi, KugouApi *kgApi, IHttpClient *http,
                    QObject *parent = nullptr);
    ~LyricDownloader();

    // Download lyrics from multiple sources, save to savePath.
    // Returns lyrics text on success, empty on failure.
    // savedPath receives the actual path where file was written.
    QString downloadAndSaveLyrics(const DMusic::MediaMeta &meta,
                                  const QString &savePath,
                                  QString *savedPath = nullptr);

    // Search for lyrics from multiple sources
    QList<LyricSearchResult> searchLyrics(const QString &keyword);
    // Get lyrics by NetEase song ID
    QString getLyricsFromNetEase(qint64 songId);
    // Get lyrics from LRCLIB by search result
    QString getLyricsFromLrclib(const LyricSearchResult &result);
    // Get lyrics from Kugou by search result
    QString getLyricsFromKugou(const LyricSearchResult &result);

    // Returns true if the last request sequence hit a network failure
    // (timeout or HTTP error, excluding 429 rate-limiting retries).
    // Lets the UI distinguish "no lyrics found" from "network error".
    bool networkError() const;

private:
    // Try each source in order
    QString fetchFromNetEase(const DMusic::MediaMeta &meta);
    QString fetchFromKugou(const DMusic::MediaMeta &meta);
    QString fetchFromLrclib(const DMusic::MediaMeta &meta);
    QString fetchLyricsFromLrclib(const DMusic::MediaMeta &meta);

    QByteArray httpGet(const QString &url);

    IHttpClient *m_http = nullptr;
    NetEaseApi *m_neApi = nullptr;
    KugouApi *m_kgApi = nullptr;
    bool m_networkError = false;
};

#endif // LYRICDOWNLOADER_H
