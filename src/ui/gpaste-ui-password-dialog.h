// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-client.h>
#include <gpaste-3/gpaste-settings.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

void g_paste_ui_password_dialog_add  (GPasteClient   *client,
                                      GPasteSettings *settings,
                                      GtkWindow      *rootwin);
void g_paste_ui_password_dialog_make (GPasteClient   *client,
                                      GPasteSettings *settings,
                                      GtkWindow      *rootwin,
                                      const gchar    *uuid);
void g_paste_ui_password_dialog_edit (GPasteClient   *client,
                                      GtkWindow      *rootwin,
                                      const gchar    *uuid);

G_END_DECLS
