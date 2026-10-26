// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-daemon.h>

G_BEGIN_DECLS

/* End the uploads out (see the definition). Exported for the standalone
 * daemon's host, which calls it just before it execs; no binding sees it. */
void g_paste_daemon_cancel_uploads (GPasteDaemon *self);

G_END_DECLS
