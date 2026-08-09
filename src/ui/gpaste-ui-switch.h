// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-client.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

GtkWidget *g_paste_ui_switch_new (GtkWindow    *topwin,
                                  GPasteClient *client);

G_END_DECLS

