/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define AK_DONT_REPLACE_STD

#include <LibCore/EventLoop.h>
#include <QThread>

class WebContentThread : public QThread
{
    Q_OBJECT
public:
    virtual ~WebContentThread() override = default;

private:
    virtual void run() override;

    Core::EventLoop m_event_loop;
};
