/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (GPASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_SETTINGS_H__
#define __G_PASTE_SETTINGS_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS            (g_paste_settings_get_type ())
#define G_PASTE_SETTINGS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_SETTINGS, GPasteSettings))
#define G_PASTE_IS_SETTINGS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_SETTINGS))
#define G_PASTE_SETTINGS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_SETTINGS, GPasteSettingsClass))
#define G_PASTE_IS_SETTINGS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_SETTINGS))
#define G_PASTE_SETTINGS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_SETTINGS, GPasteSettingsClass))

typedef struct _GPasteSettings GPasteSettings;
typedef struct _GPasteSettingsClass GPasteSettingsClass;
typedef struct _GPasteSettingsPrivate GPasteSettingsPrivate;

GType g_paste_settings_get_type (void);
gboolean g_paste_settings_get_primary_to_history (GPasteSettings *self);
guint g_paste_settings_get_max_history_size (GPasteSettings *self);
guint g_paste_settings_get_max_displayed_history_size (GPasteSettings *self);
gboolean g_paste_settings_get_synchronize_clipboards (GPasteSettings *self);
void g_paste_settings_set_tracking_state (GPasteSettings *self,
                                          gboolean        value);
gboolean g_paste_settings_get_track_changes (GPasteSettings *self);
gboolean g_paste_settings_get_sync_state_with_extension (GPasteSettings *self);
gboolean g_paste_settings_get_save_history (GPasteSettings *self);
gboolean g_paste_settings_get_trim_items (GPasteSettings *self);
const gchar *g_paste_settings_get_keyboard_shortcut (GPasteSettings *self);
GPasteSettings *g_paste_settings_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_H__*/
