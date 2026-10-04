// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lyrichttpclient.h"

#include <QEventLoop>
#include <QTimer>
#include <QNetworkRequest>
#include <QThread>

#include "util/log.h"

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
    , m_netManager(new QNetworkAccessManager(this))
{
}

QByteArray HttpClient::get(const QString &url)
{
    const int maxRetries = 3;
    const int timeoutMs = 15000;

    m_networkError = false;

    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (attempt > 0) QThread::msleep(1000 * attempt);

        QNetworkRequest request = QNetworkRequest(QUrl(url));
        request.setHeader(QNetworkRequest::UserAgentHeader, "deepin-music/1.0");

        QEventLoop loop;
        QNetworkReply *reply = m_netManager->get(request);
        QTimer timer;
        timer.setSingleShot(true);
        timer.start(timeoutMs);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError && reply->isFinished()) {
            QByteArray result = reply->readAll();
            reply->deleteLater();
            return result;
        }

        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429) {
            qCWarning(dmMusic) << "Rate limited (429), retry" << (attempt + 1);
            reply->deleteLater();
            continue;
        }

        // Genuine failure: timeout or HTTP error (non-429).
        m_networkError = true;
        reply->deleteLater();
        return QByteArray();
    }

    return QByteArray();
}
