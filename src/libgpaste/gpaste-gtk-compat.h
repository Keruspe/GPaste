/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __GPASTE_GTK_COMPAT_H__
#define __GPASTE_GTK_COMPAT_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#if GTK_API > 3
#  define gtk_widget_show_all(widget) gtk_widget_show (widget)
#else
#  define gtk_box_pack_start(box, child, expand, fill) gtk_box_pack_start (box, child, expand, fill, 0)
#  define gtk_box_pack_end(box, child, expand, fill) gtk_box_pack_end (box, child, expand, fill, 0)
#endif

G_END_DECLS

#endif /*__GPASTE_GTK_COMPAT_H__*/
