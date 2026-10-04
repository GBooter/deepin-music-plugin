// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICHTTPCLIENT_H
#define LYRICHTTPCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "lyricdownloader.h"   // for IHttpClient

// 真实的同步 HTTP 客户端：封装重试 / 429 限流 / 超时逻辑。
// 从 LyricDownloader::httpGet 抽出，使 LyricDownloader 只依赖 IHttpClient 抽象，
// 从而可在测试中替换为 mock。
class HttpClient : public QObject, public IHttpClient
{
    Q_OBJECT
public:
    explicit HttpClient(QObject *parent = nullptr);

    QByteArray get(const QString &url) override;
    bool networkError() const override { return m_networkError; }

private:
    QNetworkAccessManager *m_netManager = nullptr;
    bool m_networkError = false;
};

#endif // LYRICHTTPCLIENT_H
