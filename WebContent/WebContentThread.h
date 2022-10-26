/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define AK_DONT_REPLACE_STD

#include <QThread>

class WebContentThread : public QThread
{
    Q_OBJECT
public:
    WebContentThread(QObject* parent) : QThread(parent) {}
    virtual ~WebContentThread() override = default;

    // Expose exec() to web_content_main
    int exec_event_loop() { return exec(); }

protected:
    void run() override;
};
