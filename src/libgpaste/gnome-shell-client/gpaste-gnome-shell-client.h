/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_GNOME_SHELL_CLIENT_H__
#define __G_PASTE_GNOME_SHELL_CLIENT_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_GNOME_SHELL_BUS_NAME "org.gnome.Shell"

/* ShellKeyBindingMode stolen from gnome-shell */
/**
 * GPasteGnomeShellKeyBindingMode:
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_NONE: block keybinding
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_NORMAL: allow keybinding when in window mode,
 *     e.g. when the focus is in an application window
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_OVERVIEW: allow keybinding while the overview
 *     is active
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_LOCK_SCREEN: allow keybinding when the screen
 *     is locked, e.g. when the screen shield is shown
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_UNLOCK_SCREEN: allow keybinding in the unlock
 *     dialog
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_LOGIN_SCREEN: allow keybinding in the login screen
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_MESSAGE_TRAY: allow keybinding while the message
 *     tray is popped up
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_SYSTEM_MODAL: allow keybinding when a system modal
 *     dialog (e.g. authentification or session dialogs) is open
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_LOOKING_GLASS: allow keybinding in looking glass
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_TOPBAR_POPUP: allow keybinding while a top bar menu
 *     is open
 * @G_PASTE_GNOME_SHELL_KEYBINDING_MODE_ALL: always allow keybinding
 *
 * Controls in which GNOME Shell states a keybinding should be handled.
*/
typedef enum {
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_NONE          = 0,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_NORMAL        = 1 << 0,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_OVERVIEW      = 1 << 1,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_LOCK_SCREEN   = 1 << 2,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_UNLOCK_SCREEN = 1 << 3,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_LOGIN_SCREEN  = 1 << 4,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_MESSAGE_TRAY  = 1 << 5,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_SYSTEM_MODAL  = 1 << 6,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_LOOKING_GLASS = 1 << 7,
  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_TOPBAR_POPUP  = 1 << 8,

  G_PASTE_GNOME_SHELL_KEYBINDING_MODE_ALL = ~0,
} GPasteGnomeShellKeyBindingMode;

#define G_PASTE_TYPE_GNOME_SHELL_CLIENT            (g_paste_gnome_shell_client_get_type ())
#define G_PASTE_GNOME_SHELL_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_GNOME_SHELL_CLIENT, GPasteGnomeShellClient))
#define G_PASTE_IS_GNOME_SHELL_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_GNOME_SHELL_CLIENT))
#define G_PASTE_GNOME_SHELL_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_GNOME_SHELL_CLIENT, GPasteGnomeShellClientClass))
#define G_PASTE_IS_GNOME_SHELL_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_GNOME_SHELL_CLIENT))
#define G_PASTE_GNOME_SHELL_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_GNOME_SHELL_CLIENT, GPasteGnomeShellClientClass))

typedef struct _GPasteGnomeShellClient GPasteGnomeShellClient;
typedef struct _GPasteGnomeShellClientClass GPasteGnomeShellClientClass;

typedef struct
{
    const gchar                   *accelerator;
    GPasteGnomeShellKeyBindingMode flags;
} GPasteGnomeShellAccelerator;

#define G_PASTE_GNOME_SHELL_ACCELERATOR(accelerator) ((GPasteGnomeShellAccelerator) {accelerator, G_PASTE_GNOME_SHELL_KEYBINDING_MODE_ALL})

G_PASTE_VISIBLE
GType g_paste_gnome_shell_client_get_type (void);

/*******************/
/* Methods /  Sync */
/*******************/

guint32  g_paste_gnome_shell_client_grab_accelerator_sync   (GPasteGnomeShellClient      *self,
                                                             GPasteGnomeShellAccelerator  accelerator,
                                                             GError                     **error);
guint32 *g_paste_gnome_shell_client_grab_accelerators_sync  (GPasteGnomeShellClient      *self,
                                                             GPasteGnomeShellAccelerator *accelerators,
                                                             GError                     **error);
gboolean g_paste_gnome_shell_client_ungrab_accelerator_sync (GPasteGnomeShellClient      *self,
                                                             guint32                      action,
                                                             GError                     **error);

/********************/
/* Methods /  Async */
/********************/

void g_paste_gnome_shell_client_grab_accelerator   (GPasteGnomeShellClient      *self,
                                                    GPasteGnomeShellAccelerator  accelerator,
                                                    GAsyncReadyCallback          callback,
                                                    gpointer                     user_data);
void g_paste_gnome_shell_client_grab_accelerators  (GPasteGnomeShellClient      *self,
                                                    GPasteGnomeShellAccelerator *accelerators,
                                                    GAsyncReadyCallback          callback,
                                                    gpointer                     user_data);
void g_paste_gnome_shell_client_ungrab_accelerator (GPasteGnomeShellClient      *self,
                                                    guint32                      action,
                                                    GAsyncReadyCallback          callback,
                                                    gpointer                     user_data);

/*****************************/
/* Methods /  Async - Finish */
/*****************************/

guint32  g_paste_gnome_shell_client_grab_accelerator_finish   (GPasteGnomeShellClient *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);
guint32 *g_paste_gnome_shell_client_grab_accelerators_finish  (GPasteGnomeShellClient *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);
gboolean g_paste_gnome_shell_client_ungrab_accelerator_finish (GPasteGnomeShellClient *self,
                                                               GAsyncResult           *result,
                                                               GError                **error);

/****************/
/* Constructors */
/****************/

GPasteGnomeShellClient *g_paste_gnome_shell_client_new_sync   (GError **error);
void                    g_paste_gnome_shell_client_new        (GAsyncReadyCallback callback,
                                                               gpointer            user_data);
GPasteGnomeShellClient *g_paste_gnome_shell_client_new_finish (GAsyncResult       *result,
                                                               GError            **error);

G_END_DECLS

#endif /*__G_PASTE_GNOME_SHELL_CLIENT_H__*/
