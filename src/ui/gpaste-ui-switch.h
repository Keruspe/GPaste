/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UI_SWITCH_H__
#define __G_PASTE_UI_SWITCH_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_SWITCH (g_paste_ui_switch_get_type ())

G_PASTE_FINAL_TYPE (UiSwitch, ui_switch, UI_SWITCH, GtkSwitch)

GtkWidget *g_paste_ui_switch_new (GtkWindow    *topwin,
                                  GPasteClient *client);

G_END_DECLS

#endif /*__G_PASTE_UI_SWITCH_H__*/
