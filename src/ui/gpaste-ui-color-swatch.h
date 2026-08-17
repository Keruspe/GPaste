// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

gboolean g_paste_ui_color_swatch_set_color (GtkWidget   *self,
                                            const gchar *color);

GtkWidget *g_paste_ui_color_swatch_new (void);

G_END_DECLS
