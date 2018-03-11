/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_CLIPBOARDS_MANAGER_H__
#define __G_PASTE_CLIPBOARDS_MANAGER_H__

#include <gpaste-clipboard.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARDS_MANAGER (g_paste_clipboards_manager_get_type ())

G_PASTE_FINAL_TYPE (ClipboardsManager, clipboards_manager, CLIPBOARDS_MANAGER, GObject)

void     g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                                   GPasteClipboard         *clipboard);
void     g_paste_clipboards_manager_sync_from_to  (GPasteClipboardsManager *self,
                                                   gboolean                 from_clipboard);
void     g_paste_clipboards_manager_activate      (GPasteClipboardsManager *self);
gboolean g_paste_clipboards_manager_select        (GPasteClipboardsManager *self,
                                                   GPasteItem              *item);
void g_paste_clipboards_manager_store             (GPasteClipboardsManager *self);

GPasteClipboardsManager *g_paste_clipboards_manager_new (GPasteHistory  *history,
                                                         GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_CLIPBOARDS_MANAGER_H__*/
