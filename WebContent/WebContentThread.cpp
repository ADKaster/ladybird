/*
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define AK_DONT_REPLACE_STD

#include "WebContentThread.h"
#include <AK/Error.h>
#include <AK/Format.h>

extern ErrorOr<int> web_content_main(WebContentThread* context);

void WebContentThread::run() {
    auto err = web_content_main(this);
    if (err.is_error()) {
        warnln("WebContent failed with error: {}", err.error());
        exit(err.error().code());
    } else {
        exit(err.value());
    }
}
