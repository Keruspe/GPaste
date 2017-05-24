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
#  define gtk_box_pack_start(box, child, expand, fill) gtk_box_pack_start (box, child)
#else
#  define gtk_box_pack_start(box, child, expand, fill) gtk_box_pack_start (box, child, expand, fill, 0)
#  define gtk_box_pack_end(box, child) gtk_box_pack_end (box, child, FALSE, TRUE, 0)
#  define gtk_widget_measure(widget, orientation, for_size, minimum, natural, minimum_baseline, natural_baseline) gtk_widget_get_preferred_height (widget, minimum, natural)
#endif

G_END_DECLS

#endif /*__GPASTE_GTK_COMPAT_H__*/
