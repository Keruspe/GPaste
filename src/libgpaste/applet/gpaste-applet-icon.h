/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_APPLET_ICON_H__
#define __G_PASTE_APPLET_ICON_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_ICON (g_paste_applet_icon_get_type ())

G_PASTE_DERIVABLE_TYPE (AppletIcon, applet_icon, APPLET_ICON, GObject)

struct _GPasteAppletIconClass
{
    GObjectClass parent_class;
};

void g_paste_applet_icon_activate (void);

GPasteAppletIcon *g_paste_applet_icon_new (GType         type,
                                           GPasteClient *client);

typedef GPasteAppletIcon *(*GPasteStatusIconFunc) (GPasteClient *client,
                                                   GApplication *application);

G_END_DECLS

#endif /*__G_PASTE_APPLET_ICON_H__*/
