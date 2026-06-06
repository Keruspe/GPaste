// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-client.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_REEXEC (g_paste_ui_reexec_get_type ())

G_PASTE_FINAL_TYPE (UiReexec, ui_reexec, UI_REEXEC, GtkButton)

GtkWidget *g_paste_ui_reexec_new (GtkWindow    *topwin,
                                  GPasteClient *client);

G_END_DECLS

