// Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <util/singleton.h>
#include <DSettings>

namespace Dtk {
namespace Core {
class QSettingBackend;
}
}
using Dtk::Core::QSettingBackend;

class MusicSettings : public QObject
{
    Q_OBJECT
public:
    explicit MusicSettings(QObject *parent = nullptr);
    ~MusicSettings();

    void init();
    void ensureDefaultCover();
    QPointer<Dtk::Core::DSettings> settings();

    void sync();
    void reset();
    QVariant value(const QString &key);
    void setValue(const QString &key, const QVariant &value);

private:
    QPointer<Dtk::Core::DSettings> m_settings = nullptr;
    // backend 由 DSettings::setBackend() 接管，但 DTK 内部会把它 moveToThread
    // 到自己的 worker（见 libdtk6core 中 8 处 moveToThread 调用）。
    // 若构造时传 m_settings 当 parent，moveToThread 会因「对象有 parent」而失败，
    // 刷出 "QObject::moveToThread: Cannot move objects with a parent"，
    // 并且 backend 仍留在主线程 → 跨线程访问时可能触发
    // "Dead lock detected in BlockingQueuedConnection: Receiver is Dtk::Core::QSettingBackend"。
    // 因此显式构造**无 parent** 的 backend，并在此持有裸指针，
    // 析构时（settings 析构后）再 delete，避免泄漏且不干扰 DTK 的线程切换。
    QSettingBackend *m_backend = nullptr;
};
