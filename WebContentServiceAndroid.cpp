/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebContentServiceAndroid.h"

#include <AK/OwnPtr.h>
#include <AK/Function.h>
#include <QJniObject>
#include <QtCore/private/qandroidextras_p.h>

#ifndef AK_OS_ANDROID
#    error This file is for Android only, check CMake config!
#endif


WebContentServiceConnection::WebContentServiceConnection(WebContentServiceAndroid& owner)
    : QAndroidServiceConnection()
    , m_owner(owner)
{
    m_owner.m_connections.append(this);
}

WebContentServiceConnection::~WebContentServiceConnection()
{
    m_owner.m_connections.remove_all_matching([this](auto& a) { return a == this; });
}

void WebContentServiceConnection::onServiceConnected(QString const& name, QAndroidBinder const& serviceBinder)
{
    qDebug() << "Service " << name << " connected";
    m_binder = serviceBinder;

    if (on_service_connected)
        on_service_connected(m_binder);
}

void WebContentServiceConnection::onServiceDisconnected(QString const& name)
{
     qDebug() << "Service " << name << " disconnected";
}

static WebContentServiceAndroid* s_the;

WebContentServiceAndroid::WebContentServiceAndroid(QObject *parent)
    : QObject{parent}
{
    s_the = this;
}

WebContentServiceAndroid::~WebContentServiceAndroid()
{
    s_the = nullptr;
}

WebContentServiceAndroid& WebContentServiceAndroid::the()
{
    return *s_the;
}

OwnPtr<AndroidClientState> WebContentServiceAndroid::add_client(int ipc_fd, int fd_passing_fd)
{
    qDebug() << "Binding WebContentService";
    QAndroidIntent intent(QNativeInterface::QAndroidApplication::context(),
                    "org/serenityos/ladybird/WebContentService");

    auto connection = make<WebContentServiceConnection>(*this);
    connection->on_service_connected = [ipc_fd, fd_passing_fd](QAndroidBinder const& binder) {
        QJniObject bundle = QJniObject::callStaticMethod<jobject>(
                "org/serenityos/ladybird/WebContentService",
                "bundleFileDescriptors",
                "(II)Landroid/os/Bundle;",
                ipc_fd, fd_passing_fd
        );
        QAndroidParcel data;
        data.handle().callMethod<void>("writeBundle", "(Landroid/os/Bundle;)V", bundle.object());
        qDebug() << "Sending fds " << ipc_fd << " and " << fd_passing_fd << "to webcontent service";
        binder.transact(1, data);
    };

    [[maybe_unused]] bool ret = QtAndroidPrivate::bindService(intent, *connection, QtAndroidPrivate::BindFlag::AutoCreate);
    VERIFY(ret);

    qDebug() << "WebContentService bound properly (hopefully)";
    return connection;
}
