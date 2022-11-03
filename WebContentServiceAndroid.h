/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define AK_DONT_REPLACE_STD
#include <AK/Function.h>
#include <AK/Vector.h>
#include <QObject>
#include <QtCore/private/qandroidextras_p.h>

#ifndef AK_OS_ANDROID
#    error This file is for Android only, check CMake config!
#endif

class WebContentServiceAndroid;

class WebContentServiceConnection : public QAndroidServiceConnection
{
public:
    WebContentServiceConnection(WebContentServiceAndroid& owner);
    ~WebContentServiceConnection();
    Function<void(QAndroidBinder const&)> on_service_connected;

private:
    virtual void onServiceConnected(QString const& name, QAndroidBinder const& serviceBinder) override;
    virtual void onServiceDisconnected(QString const& name) override;
    QAndroidBinder m_binder;
    WebContentServiceAndroid& m_owner;
};

using AndroidClientState = WebContentServiceConnection;

class WebContentServiceAndroid : public QObject
{
    Q_OBJECT
public:
    explicit WebContentServiceAndroid(QObject *parent = nullptr);
    ~WebContentServiceAndroid();

    OwnPtr<AndroidClientState> add_client(int fd, int fd2);

    static WebContentServiceAndroid& the();

private:
    friend class WebContentServiceConnection;
    Vector<WebContentServiceConnection*> m_connections;
};
