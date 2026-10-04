// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// LyricDownloader 的"编排"单元测试：借助注入式构造（IHttpClient / NetEaseApi /
// KugouApi 均可被 mock 替换），在完全无真实网络的前提下验证三大能力：
//   1) 回退顺序：Kugou → NetEase → LRCLIB，前序命中则后序不被调用；
//   2) "无歌词" vs "网络错误"的判定（networkError() 语义）；
//   3) 保存 / 缓存回退 / searchLyrics 聚合 / 各源 getLyricsFrom*。
//
// 设计要点：本文件直接包含 lyricdownloader.h / lyricapi_*.h / global.h（均含 Q_OBJECT）。
// 由于 libdmusic 以 SHARED 方式构建，测试可执行里再生成一份 moc 不会与 .so 内的符号
// 发生静态合并，故不会触发重复符号链接错误（参见项目记忆中关于 AUTOMOC 的说明）。

#include <gtest/gtest.h>

#include <QObject>
#include <QFile>
#include <QDir>
#include <QFileInfo>

#include "global.h"            // DMusic::MediaMeta, DmGlobal::cachePath()
#include "lyricdownloader.h"   // LyricDownloader, IHttpClient, LyricSearchResult
#include "lyricapi_ne.h"       // NetEaseApi, NesearchResult
#include "lyricapi_kg.h"       // KugouApi, KgsearchResult

namespace {

// ---------------------------------------------------------------------------
// Mock 网络层：按 URL 子串返回脚本化响应，或模拟网络错误 / 空响应。
// ---------------------------------------------------------------------------
class FakeHttpClient : public IHttpClient
{
public:
    enum Mode { ReturnData, ReturnEmpty, FailNetwork };

    explicit FakeHttpClient(Mode mode = ReturnData) : m_mode(mode) {}

    QByteArray get(const QString &url) override
    {
        m_requests.append(url);
        if (m_mode == FailNetwork) return QByteArray();
        if (m_mode == ReturnEmpty)  return QByteArray();
        if (url.contains("/api/search")) return m_searchResponse;
        if (url.contains("/api/get"))    return m_getResponse;
        return QByteArray();
    }

    bool networkError() const override { return m_mode == FailNetwork; }

    void setSearchResponse(const QByteArray &body) { m_searchResponse = body; }
    void setGetResponse(const QByteArray &body)    { m_getResponse = body; }

    QStringList requests() const { return m_requests; }
    int callCount() const { return m_requests.size(); }

private:
    Mode         m_mode = ReturnData;
    QByteArray   m_searchResponse;
    QByteArray   m_getResponse;
    QStringList  m_requests;
};

// ---------------------------------------------------------------------------
// Mock 网易云 source：覆写三个虚方法，记录调用次数，返回脚本化结果。
// ---------------------------------------------------------------------------
class FakeNetEaseApi : public NetEaseApi
{
public:
    QString searchAndGetLyrics(const DMusic::MediaMeta &) override
    {
        ++m_sagCalls;
        return m_sagResult;
    }
    QList<NesearchResult> searchSongs(const QString &) override
    {
        ++m_searchCalls;
        return m_searchResults;
    }
    QString getLyrics(qint64) override
    {
        ++m_getCalls;
        return m_getResult;
    }

    void setSearchAndGet(const QString &s) { m_sagResult = s; }
    void setSearch(const QList<NesearchResult> &r) { m_searchResults = r; }
    void setGet(const QString &s) { m_getResult = s; }

    int sagCalls() const { return m_sagCalls; }
    int searchCalls() const { return m_searchCalls; }
    int getCalls() const { return m_getCalls; }

private:
    QString             m_sagResult;
    QList<NesearchResult> m_searchResults;
    QString             m_getResult;
    int m_sagCalls = 0, m_searchCalls = 0, m_getCalls = 0;
};

// ---------------------------------------------------------------------------
// Mock 酷狗 source：同网易云。
// ---------------------------------------------------------------------------
class FakeKugouApi : public KugouApi
{
public:
    QString searchAndGetLyrics(const DMusic::MediaMeta &) override
    {
        ++m_sagCalls;
        return m_sagResult;
    }
    QList<KgsearchResult> searchSongs(const QString &) override
    {
        ++m_searchCalls;
        return m_searchResults;
    }
    QString getLyrics(const KgsearchResult &) override
    {
        ++m_getCalls;
        return m_getResult;
    }

    void setSearchAndGet(const QString &s) { m_sagResult = s; }
    void setSearch(const QList<KgsearchResult> &r) { m_searchResults = r; }
    void setGet(const QString &s) { m_getResult = s; }

    int sagCalls() const { return m_sagCalls; }
    int searchCalls() const { return m_searchCalls; }
    int getCalls() const { return m_getCalls; }

private:
    QString             m_sagResult;
    QList<KgsearchResult> m_searchResults;
    QString             m_getResult;
    int m_sagCalls = 0, m_searchCalls = 0, m_getCalls = 0;
};

// ---------------------------------------------------------------------------
// 构造测试用 MediaMeta
// ---------------------------------------------------------------------------
DMusic::MediaMeta makeMeta(const QString &title, const QString &artist)
{
    DMusic::MediaMeta m;
    m.localPath = QDir::temp().filePath(title + ".mp3");
    m.title = title;
    m.artist = artist;
    m.hash = "hash_" + title;
    return m;
}

// ===========================================================================
// 1) 回退顺序
// ===========================================================================
TEST(LyricDownloaderTest, Fallback_KugouWins_NextSourcesNotCalled)
{
    FakeKugouApi kg;   kg.setSearchAndGet("[00:01.00]kugou line");
    FakeNetEaseApi ne; ne.setSearchAndGet("[00:02.00]ne line");
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);  // LRCLIB 不参与

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"),
                                             QDir::temp().filePath("out.lrc"), &saved);

    EXPECT_EQ(lyrics, QString("[00:01.00]kugou line"));
    EXPECT_EQ(kg.sagCalls(), 1);
    EXPECT_EQ(ne.sagCalls(), 0);   // 酷狗命中，网易云不应被调用
    EXPECT_EQ(http.callCount(), 0); // LRCLIB 不应被调用
    EXPECT_FALSE(dl.networkError());
}

TEST(LyricDownloaderTest, Fallback_KugouEmpty_ThenNetEase)
{
    FakeKugouApi kg;   kg.setSearchAndGet(QString());   // 空
    FakeNetEaseApi ne; ne.setSearchAndGet("[00:02.00]ne line");
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"),
                                             QDir::temp().filePath("out.lrc"), &saved);

    EXPECT_EQ(lyrics, QString("[00:02.00]ne line"));
    EXPECT_EQ(kg.sagCalls(), 1);
    EXPECT_EQ(ne.sagCalls(), 1);
    EXPECT_EQ(http.callCount(), 0); // LRCLIB 仍不被调用
    EXPECT_FALSE(dl.networkError());
}

TEST(LyricDownloaderTest, Fallback_AllEmpty_ReachesLrclib)
{
    FakeKugouApi kg;   kg.setSearchAndGet(QString());
    FakeNetEaseApi ne; ne.setSearchAndGet(QString());
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);  // LRCLIB 查询返回空

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"),
                                             QDir::temp().filePath("out.lrc"), &saved);

    EXPECT_TRUE(lyrics.isEmpty());
    EXPECT_EQ(kg.sagCalls(), 1);
    EXPECT_EQ(ne.sagCalls(), 1);
    EXPECT_GE(http.callCount(), 1);   // 至少发起一次 LRCLIB 搜索
    EXPECT_FALSE(dl.networkError());   // 空响应（无网络错误）→ 不算网络错误
}

// ===========================================================================
// 2) "无歌词" vs "网络错误" 判定
// ===========================================================================
TEST(LyricDownloaderTest, NoLyrics_AllEmpty_NotNetworkError)
{
    FakeKugouApi kg;   kg.setSearchAndGet(QString());
    FakeNetEaseApi ne; ne.setSearchAndGet(QString());
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"),
                                             QDir::temp().filePath("out.lrc"), &saved);

    EXPECT_TRUE(lyrics.isEmpty());
    EXPECT_FALSE(dl.networkError());   // 仅"没找到"，不是网络故障
}

TEST(LyricDownloaderTest, NetworkError_LrclibFails)
{
    FakeKugouApi kg;   kg.setSearchAndGet(QString());
    FakeNetEaseApi ne; ne.setSearchAndGet(QString());
    FakeHttpClient http(FakeHttpClient::FailNetwork);  // LRCLIB 网络层失败

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"),
                                             QDir::temp().filePath("out.lrc"), &saved);

    EXPECT_TRUE(lyrics.isEmpty());
    EXPECT_TRUE(dl.networkError());    // 真实网络故障 → 标记 networkError
}

// ===========================================================================
// 3) 保存 / 缓存回退
// ===========================================================================
TEST(LyricDownloaderTest, SaveToGivenPath_WritesFile)
{
    FakeKugouApi kg;   kg.setSearchAndGet("[00:01.00]saved line");
    FakeNetEaseApi ne; ne.setSearchAndGet(QString());
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);

    const QString path = QDir::temp().filePath("dl_save_test.lrc");
    QFile::remove(path);

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"), path, &saved);

    EXPECT_FALSE(lyrics.isEmpty());
    EXPECT_EQ(saved, path);
    EXPECT_TRUE(QFile::exists(path));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    EXPECT_EQ(QString::fromUtf8(f.readAll()), QString("[00:01.00]saved line"));
    f.close();
    QFile::remove(path);
}

TEST(LyricDownloaderTest, CacheFallback_WhenPrimarySaveFails)
{
    FakeKugouApi kg;   kg.setSearchAndGet("[00:01.00]cached line");
    FakeNetEaseApi ne; ne.setSearchAndGet(QString());
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);

    // DmGlobal::cachePath() 在单测里默认为空（DmGlobal::init 未调用，回退路径会变成
    // 不可写的 /lyrics）。这里显式指向一个可写临时目录，让缓存回退真正落盘。
    const QString cacheRoot = QDir::temp().filePath("dl_cache_root");
    DmGlobal::setCachePath(cacheRoot);
    QDir().mkpath(cacheRoot);

    // 指向不存在的目录 → 主路径写入失败 → 应回退到 cache 目录
    const QString badPath = QDir::temp().filePath("no_such_dir_xyz/dl_test.lrc");

    LyricDownloader dl(&ne, &kg, &http);
    QString saved;
    QString lyrics = dl.downloadAndSaveLyrics(makeMeta("Song", "Artist"), badPath, &saved);

    EXPECT_FALSE(lyrics.isEmpty());
    // 回退路径应位于缓存目录的 lyrics 子目录下，并按 hash 命名
    const QString cacheFile = DmGlobal::cachePath() + "/lyrics/hash_Song.lrc";
    EXPECT_EQ(saved, cacheFile);
    EXPECT_TRUE(QFile::exists(cacheFile));
    QFile::remove(cacheFile);
}

// ===========================================================================
// 4) searchLyrics 聚合（顺序：Kugou → NetEase → LRCLIB）
// ===========================================================================
TEST(LyricDownloaderTest, SearchLyrics_AggregatesSourcesInOrder)
{
    FakeKugouApi kg;
    {
        KgsearchResult r; r.title = "KSong"; r.artist = "KArtist"; r.hash = "kh";
        kg.setSearch(QList<KgsearchResult>() << r);
    }
    FakeNetEaseApi ne;
    {
        NesearchResult r; r.title = "NSong"; r.artist = "NArtist"; r.id = 99;
        ne.setSearch(QList<NesearchResult>() << r);
    }
    FakeHttpClient http(FakeHttpClient::ReturnData);
    http.setSearchResponse(R"([{"trackName":"LSong","artistName":"LArtist",)"
                           R"("albumName":"LAlbum","duration":200.0}])");

    LyricDownloader dl(&ne, &kg, &http);
    QList<LyricSearchResult> results = dl.searchLyrics("any keyword");

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0].source, QString("Kugou"));
    EXPECT_EQ(results[0].title, QString("KSong"));
    EXPECT_EQ(results[1].source, QString("NetEase"));
    EXPECT_EQ(results[1].title, QString("NSong"));
    EXPECT_EQ(results[2].source, QString("LRCLIB"));
    EXPECT_EQ(results[2].title, QString("LSong"));
    EXPECT_EQ(kg.searchCalls(), 1);
    EXPECT_EQ(ne.searchCalls(), 1);
}

TEST(LyricDownloaderTest, SearchLyrics_NetworkErrorFlagSet)
{
    FakeKugouApi kg;   kg.setSearch(QList<KgsearchResult>());
    FakeNetEaseApi ne; ne.setSearch(QList<NesearchResult>());
    FakeHttpClient http(FakeHttpClient::FailNetwork);

    LyricDownloader dl(&ne, &kg, &http);
    QList<LyricSearchResult> results = dl.searchLyrics("any keyword");

    EXPECT_TRUE(results.isEmpty());
    EXPECT_TRUE(dl.networkError());   // LRCLIB 网络失败 → 置位
}

TEST(LyricDownloaderTest, SearchLyrics_NetworkErrorResetAtStart)
{
    FakeKugouApi kg;   kg.setSearch(QList<KgsearchResult>());
    FakeNetEaseApi ne; ne.setSearch(QList<NesearchResult>());
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);  // 无网络错误

    LyricDownloader dl(&ne, &kg, &http);
    dl.searchLyrics("any keyword");
    EXPECT_FALSE(dl.networkError());   // 每次 searchLyrics 开头重置标记
}

// ===========================================================================
// 5) 各源 getLyricsFrom*
// ===========================================================================
TEST(LyricDownloaderTest, GetLyricsFromKugou)
{
    FakeKugouApi kg;   kg.setGet("[00:01.00]kg get");
    FakeNetEaseApi ne;
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);

    LyricDownloader dl(&ne, &kg, &http);
    LyricSearchResult r;
    r.kugouHash = "h";
    EXPECT_EQ(dl.getLyricsFromKugou(r), QString("[00:01.00]kg get"));
}

TEST(LyricDownloaderTest, GetLyricsFromNetEase)
{
    FakeKugouApi kg;
    FakeNetEaseApi ne; ne.setGet("[00:02.00]ne get");
    FakeHttpClient http(FakeHttpClient::ReturnEmpty);

    LyricDownloader dl(&ne, &kg, &http);
    EXPECT_EQ(dl.getLyricsFromNetEase(123), QString("[00:02.00]ne get"));
}

TEST(LyricDownloaderTest, GetLyricsFromLrclib_PrefersSynced)
{
    FakeKugouApi kg;
    FakeNetEaseApi ne;
    FakeHttpClient http(FakeHttpClient::ReturnData);
    http.setGetResponse(R"({"syncedLyrics":"[00:01.00]synced",)"
                        R"("plainLyrics":"[00:02.00]plain"})");

    LyricDownloader dl(&ne, &kg, &http);
    LyricSearchResult r;
    r.lrclibTrackName = "T"; r.lrclibArtistName = "A";
    r.lrclibAlbumName = "B"; r.duration = 180000;
    EXPECT_EQ(dl.getLyricsFromLrclib(r), QString("[00:01.00]synced"));
}

TEST(LyricDownloaderTest, GetLyricsFromLrclib_PlainFallback)
{
    FakeKugouApi kg;
    FakeNetEaseApi ne;
    FakeHttpClient http(FakeHttpClient::ReturnData);
    http.setGetResponse(R"({"plainLyrics":"[00:02.00]only plain"})");

    LyricDownloader dl(&ne, &kg, &http);
    LyricSearchResult r;
    r.lrclibTrackName = "T"; r.lrclibArtistName = "A";
    r.lrclibAlbumName = "B"; r.duration = 180000;
    EXPECT_EQ(dl.getLyricsFromLrclib(r), QString("[00:02.00]only plain"));
}

TEST(LyricDownloaderTest, GetLyricsFromLrclib_ErrorFieldReturnsEmpty)
{
    FakeKugouApi kg;
    FakeNetEaseApi ne;
    FakeHttpClient http(FakeHttpClient::ReturnData);
    http.setGetResponse(R"({"error":"not found"})");

    LyricDownloader dl(&ne, &kg, &http);
    LyricSearchResult r;
    r.lrclibTrackName = "T"; r.lrclibArtistName = "A";
    r.lrclibAlbumName = "B"; r.duration = 180000;
    EXPECT_TRUE(dl.getLyricsFromLrclib(r).isEmpty());
}

} // namespace
