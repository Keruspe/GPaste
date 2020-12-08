/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2021, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_CONTENT_PROVIDER_H__
#define __G_PASTE_CONTENT_PROVIDER_H__

#include <gpaste-item.h>

#include <gdk/gdk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CONTENT_PROVIDER (g_paste_content_provider_get_type ())

G_PASTE_FINAL_TYPE (ContentProvider, content_provider, CONTENT_PROVIDER, GdkContentProvider)

void g_paste_content_provider_set_item (GPasteContentProvider *self,
                                        GPasteItem            *item);

GPasteContentProvider *g_paste_content_provider_new (void);

G_END_DECLS

#endif /*__G_PASTE_CONTENT_PROVIDER_H__*/
