// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SWITCH_H__
#define __G_PASTE_UI_SWITCH_H__

#include <gpaste/gpaste-client.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

GtkWidget *g_paste_ui_switch_new (GtkWindow    *topwin,
                                  GPasteClient *client);

G_END_DECLS

#endif /*__G_PASTE_UI_SWITCH_H__*/
