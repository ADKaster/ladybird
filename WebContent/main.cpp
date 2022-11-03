/*
 * Copyright (c) 2020-2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define AK_DONT_REPLACE_STD

#include "../EventLoopPluginQt.h"
#include "../FontPluginQt.h"
#include "../ImageCodecPluginLadybird.h"
#include "../RequestManagerQt.h"
#include "../Utilities.h"
#include "../WebSocketClientManagerLadybird.h"
#include <AK/LexicalPath.h>
#include <LibCore/EventLoop.h>
#include <LibCore/File.h>
#include <LibCore/LocalServer.h>
#include <LibCore/System.h>
#include <LibIPC/SingleServer.h>
#include <LibMain/Main.h>
#include <LibWeb/Loader/ContentFilter.h>
#include <LibWeb/Loader/FrameLoader.h>
#include <LibWeb/Loader/ResourceLoader.h>
#include <LibWeb/WebSockets/WebSocket.h>
#include <QGuiApplication>
#include <QSocketNotifier>
#include <QTimer>
#include <WebContent/ConnectionFromClient.h>

ErrorOr<void> start_web_content();
static ErrorOr<void> load_content_filters();

extern String s_serenity_resource_root;

#ifdef AK_OS_ANDROID
#include "WebContentThread.h"
//#   include <QAndroidService>
#   include <QtCore/private/qandroidextras_p.h>

class WebContentBinder : public QAndroidBinder
{
public:
    WebContentBinder() : QAndroidBinder() {}

    ~WebContentBinder() {
        m_thread->quit();
        m_thread->wait();
    }

private:
    virtual bool onTransact(int code, QAndroidParcel const& data, QAndroidParcel const& reply, QAndroidBinder::CallType flags) override
    {
        qDebug() << "WebContentBinder: onTransact(), code " << code << ", flags " << int(flags);

        if (code != 1) {
            qDebug() << "What is code " << code << "?";
            return false;
        }

        auto parcel = data.handle();

        QJniObject bundle = parcel.callMethod<jobject>("readBundle", "()Landroid/os/Bundle;");
        QJniObject ipc_fd_handle = bundle.callMethod<jobject>("getParcelable", "(Ljava/lang/String;)Ljava/lang/Object", "IPC_SOCKET");
        QJniObject fd_passing_fd_handle = bundle.callMethod<jobject>("getParcelable", "(Ljava/lang/String;)Ljava/lang/Object", "FD_PASSING_SOCKET");

        int ipc_fd = ipc_fd_handle.callMethod<int>("detachFd");
        int fd_passing_fd = fd_passing_fd_handle.callMethod<int>("detachFd");

        auto takeover_string = String::formatted("x:{}", ipc_fd);
        MUST(Core::System::setenv("SOCKET_TAKEOVER"sv, takeover_string, true));

        auto fd_passing_socket_string = String::formatted("{}", fd_passing_fd);
        MUST(Core::System::setenv("FD_PASSING_SOCKET"sv, fd_passing_socket_string, true));

        m_thread = new WebContentThread;
        m_thread->start();
        QObject::connect(m_thread, &WebContentThread::finished, m_thread, &QObject::deleteLater);

        reply.writeVariant("OK");
        return true;
    }

    QThread* m_thread { nullptr };
};

#endif

static void web_platform_init()
{
    Web::Platform::EventLoopPlugin::install(*new Ladybird::EventLoopPluginQt);
    Web::Platform::ImageCodecPlugin::install(*new Ladybird::ImageCodecPluginLadybird);

    Web::ResourceLoader::initialize(RequestManagerQt::create());
    Web::WebSockets::WebSocketClientManager::initialize(Ladybird::WebSocketClientManagerLadybird::create());

    Web::FrameLoader::set_default_favicon_path(String::formatted("{}/res/icons/16x16/app-browser.png", s_serenity_resource_root));

    Web::Platform::FontPlugin::install(*new Ladybird::FontPluginQt);

    Web::FrameLoader::set_error_page_url(String::formatted("file://{}/res/html/error.html", s_serenity_resource_root));

    auto maybe_content_filter_error = load_content_filters();
    if (maybe_content_filter_error.is_error())
        dbgln("Failed to load content filters: {}", maybe_content_filter_error.error());
}

ErrorOr<void> start_web_content()
{
    qDebug() << "TAKING OVER SOCKET";
    auto client = TRY(IPC::take_over_accepted_client_from_system_server<WebContent::ConnectionFromClient>());
    qDebug() << "TOOK OVER SOCKET";

    auto* fd_passing_socket_spec = getenv("FD_PASSING_SOCKET");
    qDebug() << "GRABBING MY FD PASSING SOCKET " << fd_passing_socket_spec;
    VERIFY(fd_passing_socket_spec);
    auto fd_passing_socket_spec_string = String(fd_passing_socket_spec);
    auto maybe_fd_passing_socket = fd_passing_socket_spec_string.to_int();
    VERIFY(maybe_fd_passing_socket.has_value());

    qDebug() << "ADOPTING THE FD PASSING SOCKET " << maybe_fd_passing_socket.value();
    client->set_fd_passing_socket(TRY(Core::Stream::LocalSocket::adopt_fd(maybe_fd_passing_socket.value())));

    QSocketNotifier notifier(client->socket().fd().value(), QSocketNotifier::Type::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, [&] {
        client->socket().notifier()->on_ready_to_read();
    });

    struct DeferredInvokerQt final : IPC::DeferredInvoker {
        virtual ~DeferredInvokerQt() = default;
        virtual void schedule(Function<void()> callback) override
        {
            QTimer::singleShot(0, move(callback));
        }
    };

    client->set_deferred_invoker(make<DeferredInvokerQt>());

    return {};
}

ErrorOr<int> serenity_main(Main::Arguments arguments)
{

    qDebug() << "HELLOOOO FROM WEBCONTENT";

#ifdef AK_OS_ANDROID
    QAndroidService app(arguments.argc, arguments.argv, [](QAndroidIntent const&) {
        qDebug() << "onBind() for WebContent";
        return new WebContentBinder;
    });
#else
    // NOTE: This is only used for the Core::Socket inside the IPC connection.
    // FIXME: Refactor things so we can get rid of this somehow.
    Core::EventLoop event_loop;

    QGuiApplication app(arguments.argc, arguments.argv);
#endif

    platform_init();
    web_platform_init();

#ifndef AK_OS_ANDROID
    TRY(start_web_content());
#endif

    qDebug() << "WEB CONTENT EXECING";
    return app.exec();
}

static ErrorOr<void> load_content_filters()
{
    auto file_or_error = Core::Stream::File::open(String::formatted("{}/home/anon/.config/BrowserContentFilters.txt", s_serenity_resource_root), Core::Stream::OpenMode::Read);
    if (file_or_error.is_error())
        file_or_error = Core::Stream::File::open(String::formatted("{}/res/ladybird/BrowserContentFilters.txt", s_serenity_resource_root), Core::Stream::OpenMode::Read);
    if (file_or_error.is_error())
        return file_or_error.release_error();
    auto file = file_or_error.release_value();
    auto ad_filter_list = TRY(Core::Stream::BufferedFile::create(move(file)));
    auto buffer = TRY(ByteBuffer::create_uninitialized(4096));
    while (TRY(ad_filter_list->can_read_line())) {
        auto line = TRY(ad_filter_list->read_line(buffer));
        if (!line.is_empty()) {
            Web::ContentFilter::the().add_pattern(line);
        }
    }
    return {};
}
