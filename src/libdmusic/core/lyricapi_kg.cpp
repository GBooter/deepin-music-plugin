// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lyricapi_kg.h"
#include "lyriccrypt.h"
#include "lyricformat.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QRandomGenerator>
#include <QDateTime>
#include <QRegularExpression>
#include <QMutex>
#include <QDebug>

#include "util/log.h"

static const QByteArray KG_HMAC_KEY = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
static const QByteArray KG_KRC_KEY = "@Gaw^2tGQ61-\xce\xd2ni";
static const QString KG_SEARCH_URL = "https://complexsearch.kugou.com/v2/search/song";
static const QString KG_LYRICS_LIST_URL = "https://lyrics.kugou.com/v1/search";
// 歌词下载端点：优先 HTTPS（避免歌词内容明文传输），若服务端/网络环境
// 不支持则自动降级回 HTTP，由探测结果决定，避免直接改死导致下载全失败。
static const QString KG_LYRICS_DL_URL_HTTPS = "https://lyrics.kugou.com/download";
static const QString KG_LYRICS_DL_URL_HTTP  = "http://lyrics.kugou.com/download";
static const QString KG_REGISTER_URL = "https://userservice.kugou.com/risk/v1/r_register_dev";

// 设备信息有效期（毫秒）。酷狗的服务端可能在一段时间后使某个 dfid 失效，
// 缓存过久会导致所有搜索/下载持续失败且无法自愈，故设置 TTL 定期重注册。
static const qint64 KG_REG_TTL_MS = 30LL * 60 * 1000;

// 歌词下载端点的协议探测结果（-1 未探测 / 0 HTTP / 1 HTTPS），全局共享。
static QMutex s_dlSchemeMutex;
static int s_dlScheme = -1;

// 设备注册状态（跨 KugouApi 实例共享，避免每次搜索都重新注册）
static QMutex s_regMutex;
static bool s_registered = false;
static qint64 s_registeredAtMs = 0;
static QString s_dfid;
static QString s_userid;
static QString s_mid;
static QString s_uuid;

static QByteArray kgMid()
{
    qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return LyricCrypt::md5(QByteArray::number(ms)).toUtf8();
}

KugouApi::KugouApi(QObject *parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this))
{
}

KugouApi::~KugouApi() = default;

QByteArray KugouApi::krcDecrypt(const QByteArray &encrypted)
{
    // 核心算法已集中到 LyricCrypt::krcDecrypt（纯函数、可独立测试），此处仅转发。
    return LyricCrypt::krcDecrypt(encrypted, KG_KRC_KEY);
}

// 将毫秒转换为标准LRC时间戳格式 mm:ss.xx
// 已提取到 lyricformat.h（LyricFormat::msToLrcTime），供 kg / ne 共用。
using LyricFormat::msToLrcTime;

// KRC→标准LRC 转换已提取到 LyricFormat::krcToLrc（见 lyricformat.h），
// 供 KugouApi / NetEaseApi 共用，避免两份近重复实现。

QByteArray KugouApi::computeSignature(const QVariantMap &params, const QByteArray &postData)
{
    // Sort params by key, format as key=value, concatenate, compute MD5
    QStringList keys = params.keys();
    keys.sort();
    QString concat;
    for (const QString &k : keys) {
        QVariant v = params.value(k);
        if (v.canConvert<QVariantMap>()) {
            concat += k + "=" + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(v.toMap())).toJson(QJsonDocument::Compact));
        } else {
            concat += k + "=" + v.toString();
        }
    }
    concat += QString::fromUtf8(postData);
    QByteArray signSrc = KG_HMAC_KEY + concat.toUtf8() + KG_HMAC_KEY;
    return LyricCrypt::md5(signSrc).toUtf8();
}

QByteArray KugouApi::httpGet(const QString &url, const QMap<QByteArray, QByteArray> &headers)
{
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", "Android14-1070-11070-201-0-Lyric-wifi");
    request.setRawHeader("Connection", "Keep-Alive");
    request.setRawHeader("KG-Rec", "1");
    request.setRawHeader("KG-RC", "1");
    request.setRawHeader("KG-CLIENTTIMEMS", QByteArray::number(QDateTime::currentMSecsSinceEpoch()));
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key(), it.value());

    QNetworkReply *reply = m_netManager->get(request);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(15000);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray result;
    if (reply->error() == QNetworkReply::NoError) {
        result = reply->readAll();
    } else {
        qCWarning(dmMusic) << "Kugou HTTP GET error:" << url << reply->errorString();
    }
    reply->deleteLater();
    return result;
}

bool KugouApi::ensureRegistered()
{
    QMutexLocker locker(&s_regMutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // TTL 过期或 dfid 缺失时重新注册，避免服务端失效后永久不可用
    if (s_registered && !s_dfid.isEmpty() && (now - s_registeredAtMs) < KG_REG_TTL_MS) {
        m_dfid = s_dfid;
        m_userid = s_userid;
        m_mid = s_mid;
        m_uuid = s_uuid;
        return true;
    }
    if (s_registered) {
        qCInfo(dmMusic) << "Kugou device registration expired, re-registering";
        s_registered = false;
    }

    QByteArray mid = kgMid();

    QVariantMap params;
    params["appid"] = "3116";
    params["clientver"] = "11070";
    params["clienttime"] = QString::number(QDateTime::currentSecsSinceEpoch());
    params["mid"] = QString::fromUtf8(mid);
    params["dfid"] = "-";
    params["uuid"] = "-";
    params["signature"] = QString::fromUtf8(computeSignature(params));

    QUrl url(KG_REGISTER_URL);
    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value().toString());
    url.setQuery(query);

    QMap<QByteArray, QByteArray> headers;
    headers["mid"] = mid;

    QByteArray resp = httpGet(url.toString(), headers);
    QJsonDocument doc = QJsonDocument::fromJson(resp);
    QJsonObject root = doc.object();
    int status = root.value("status").toInt();
    int errCode = root.value("error_code").toInt();
    if (status != 1 && errCode != 0) {
        // 设备注册失败**不影响取词**：实测注册失败后仍能以匿名方式拿到歌词，
        // 歌词模块运行时验证（tools/lyric-runtime-check）已确认 Kugou 命中并
        // 输出逐字歌词。这里从 Warning 降为 Info：它是可预期的服务端策略变化，
        // 每次搜索/下载都会打一条Warning 只会淹没真正的错误。
        qCInfo(dmMusic) << "Kugou device register failed (non-fatal, continuing anonymously), status:"
                        << status << "error_code:" << errCode;
        return false;
    }

    QJsonObject data = root.value("data").toObject();
    QString dfid = data.value("dfid").toString();
    if (dfid.isEmpty()) {
        qCWarning(dmMusic) << "Kugou device register returned empty dfid";
        return false;
    }

    s_dfid = dfid;
    s_userid = data.value("userid").toString();
    s_mid = data.value("mid").toString();
    if (s_mid.isEmpty()) s_mid = QString::fromUtf8(mid);
    s_uuid = data.value("uuid").toString();
    if (s_uuid.isEmpty()) s_uuid = s_mid;
    s_registeredAtMs = QDateTime::currentMSecsSinceEpoch();
    s_registered = true;

    m_dfid = s_dfid;
    m_userid = s_userid;
    m_mid = s_mid;
    m_uuid = s_uuid;

    qCInfo(dmMusic) << "Kugou device registered, dfid:" << s_dfid;
    return true;
}

QList<KgsearchResult> KugouApi::searchSongs(const QString &keyword)
{
    ensureRegistered();
    QString dfid = m_dfid.isEmpty() ? "-" : m_dfid;
    QString uuid = m_uuid.isEmpty() ? "-" : m_uuid;
    QString mid = m_mid.isEmpty() ? QString::fromUtf8(kgMid()) : m_mid;
    QString userid = m_userid.isEmpty() ? "0" : m_userid;

    QVariantMap params;
    params["appid"] = "3116";
    params["clientver"] = "11070";
    params["clienttime"] = QString::number(QDateTime::currentSecsSinceEpoch());
    params["iscorrection"] = "1";
    params["uuid"] = uuid;
    params["mid"] = mid;
    params["dfid"] = dfid;
    params["platform"] = "AndroidFilter";
    params["userid"] = userid;
    params["token"] = "";
    params["keyword"] = keyword;
    params["page"] = "1";
    params["pagesize"] = "20";
    params["sorttype"] = "0";

    params["signature"] = QString::fromUtf8(computeSignature(params));

    // Build URL
    QUrl url(KG_SEARCH_URL);
    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value().toString());
    url.setQuery(query);

    QMap<QByteArray, QByteArray> headers;
    headers["x-router"] = "complexsearch.kugou.com";
    headers["mid"] = mid.toUtf8();
    if (!dfid.isEmpty() && dfid != "-")
        headers["dfid"] = dfid.toUtf8();

    QByteArray resp = httpGet(url.toString(), headers);
    QJsonDocument doc = QJsonDocument::fromJson(resp);
    QJsonObject root = doc.object();

    QList<KgsearchResult> results;
    if (root.value("error_code").toInt() != 0) return results;

    QJsonArray lists = root.value("data").toObject().value("lists").toArray();
    for (const auto &item : lists) {
        QJsonObject obj = item.toObject();
        KgsearchResult r;
        r.id = obj.value("ID").toVariant().toLongLong();
        r.hash = obj.value("FileHash").toString();
        r.title = obj.value("SongName").toString();
        QStringList singers;
        for (const auto &s : obj.value("Singers").toArray())
            singers << s.toObject().value("name").toString();
        r.artist = singers.join(", ");
        r.album = obj.value("AlbumName").toString();
        r.duration = static_cast<qint64>(obj.value("Duration").toDouble()) * 1000;
        results.append(r);
    }
    return results;
}

QString KugouApi::getLyrics(const KgsearchResult &song)
{
    ensureRegistered();
    QString dfid = m_dfid.isEmpty() ? "-" : m_dfid;
    QString uuid = m_uuid.isEmpty() ? "-" : m_uuid;
    QString mid = m_mid.isEmpty() ? QString::fromUtf8(kgMid()) : m_mid;
    QString userid = m_userid.isEmpty() ? "0" : m_userid;

    // Step 1: Get lyrics candidates
    QVariantMap listParams;
    listParams["appid"] = "3116";
    listParams["clientver"] = "11070";
    listParams["album_audio_id"] = QString::number(song.id);
    listParams["duration"] = QString::number(song.duration);
    listParams["hash"] = song.hash;
    listParams["keyword"] = song.artist + " - " + song.title;
    listParams["lrctxt"] = "1";
    listParams["man"] = "no";
    listParams["dfid"] = dfid;
    listParams["uuid"] = uuid;
    listParams["userid"] = userid;
    listParams["mid"] = mid;

    listParams["signature"] = QString::fromUtf8(computeSignature(listParams));

    QUrl listUrl(KG_LYRICS_LIST_URL);
    QUrlQuery listQuery;
    for (auto it = listParams.constBegin(); it != listParams.constEnd(); ++it)
        listQuery.addQueryItem(it.key(), it.value().toString());
    listUrl.setQuery(listQuery);

    QMap<QByteArray, QByteArray> headers;
    headers["mid"] = mid.toUtf8();
    if (!dfid.isEmpty() && dfid != "-")
        headers["dfid"] = dfid.toUtf8();

    QByteArray listResp = httpGet(listUrl.toString(), headers);
    QJsonDocument listDoc = QJsonDocument::fromJson(listResp);
    QJsonObject listRoot = listDoc.object();

    if (listRoot.value("error_code").toInt() != 0) return QString();
    QJsonArray candidates = listRoot.value("candidates").toArray();
    if (candidates.isEmpty()) return QString();

    // Pick best candidate (highest score)
    QJsonObject best = candidates.first().toObject();
    for (const auto &c : candidates) {
        QJsonObject obj = c.toObject();
        if (obj.value("score").toInt() > best.value("score").toInt())
            best = obj;
    }

    QString lyricsId = best.value("id").toString();
    QString accessKey = best.value("accesskey").toString();

    // Step 2: Download lyrics
    QVariantMap dlParams;
    dlParams["appid"] = "3116";
    dlParams["clientver"] = "11070";
    dlParams["accesskey"] = accessKey;
    dlParams["charset"] = "utf8";
    dlParams["client"] = "mobi";
    dlParams["fmt"] = "krc";
    dlParams["id"] = lyricsId;
    dlParams["ver"] = "1";
    dlParams["dfid"] = dfid;
    dlParams["uuid"] = uuid;
    dlParams["userid"] = userid;
    dlParams["mid"] = mid;

    dlParams["signature"] = QString::fromUtf8(computeSignature(dlParams));

    // 协议选择：首次使用时探测 HTTPS 是否可用，探测结果全局缓存。
    // HTTPS 下载失败（网络/服务端原因）时降级回 HTTP 一次，避免功能不可用。
    int scheme;
    {
        QMutexLocker schemeLocker(&s_dlSchemeMutex);
        scheme = s_dlScheme;
    }

    QByteArray dlResp;
    bool dlOk = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool useHttps = (attempt == 0) ? (scheme != 0) : (scheme == 0);
        QUrl dlUrl(useHttps ? KG_LYRICS_DL_URL_HTTPS : KG_LYRICS_DL_URL_HTTP);
        QUrlQuery dlQuery;
        for (auto it = dlParams.constBegin(); it != dlParams.constEnd(); ++it)
            dlQuery.addQueryItem(it.key(), it.value().toString());
        dlUrl.setQuery(dlQuery);

        dlResp = httpGet(dlUrl.toString(), headers);
        QJsonDocument dlDoc = QJsonDocument::fromJson(dlResp);
        QJsonObject dlRoot = dlDoc.object();
        if (!dlDoc.isNull() && dlRoot.value("error_code").toInt() == 0) {
            if (scheme < 0) {
                QMutexLocker schemeLocker(&s_dlSchemeMutex);
                if (s_dlScheme < 0) s_dlScheme = useHttps ? 1 : 0;
            }
            dlOk = true;
            break;
        }

        // 首次探测 HTTPS 失败：记住结果并降级重试一次
        if (attempt == 0) {
            QMutexLocker schemeLocker(&s_dlSchemeMutex);
            if (s_dlScheme < 0) s_dlScheme = 0;
            qCWarning(dmMusic) << "Kugou lyrics download over HTTPS failed, falling back to HTTP";
        }
    }

    if (!dlOk) return QString();

    QJsonObject dlRoot = QJsonDocument::fromJson(dlResp).object();
    int contentType = dlRoot.value("contenttype").toInt();
    QByteArray contentB64 = dlRoot.value("content").toString().toUtf8();
    QByteArray content = QByteArray::fromBase64(contentB64);

    if (contentType == 2) {
        // Plain text - already standard LRC
        return QString::fromUtf8(content);
    } else {
        // Encrypted KRC - decrypt and convert to standard LRC
        QString krcText = QString::fromUtf8(krcDecrypt(content));
        QString standardLrc = LyricFormat::krcToLrc(krcText);
        qCInfo(dmMusic) << "Converted KRC to standard LRC, size:" << standardLrc.size();
        return standardLrc;
    }
}

QString KugouApi::searchAndGetLyrics(const DMusic::MediaMeta &meta)
{
    QString keyword = !meta.artist.isEmpty()
        ? meta.artist + " " + meta.title : meta.title;

    QList<KgsearchResult> results = searchSongs(keyword);
    if (results.isEmpty()) return QString();

    int bestIdx = 0;
    for (int i = 0; i < results.size(); ++i) {
        bool titleOk = results[i].title.compare(meta.title, Qt::CaseInsensitive) == 0
                       || results[i].title.contains(meta.title, Qt::CaseInsensitive)
                       || meta.title.contains(results[i].title, Qt::CaseInsensitive);
        bool artistOk = meta.artist.isEmpty()
                        || results[i].artist.contains(meta.artist, Qt::CaseInsensitive)
                        || meta.artist.contains(results[i].artist, Qt::CaseInsensitive);
        if (titleOk && artistOk) { bestIdx = i; break; }
    }

    qCInfo(dmMusic) << "Kugou matched:" << results[bestIdx].title << "-" << results[bestIdx].artist;
    return getLyrics(results[bestIdx]);
}
