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

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_GNOME_SHELL_CLIENT_H__
#define __G_PASTE_GNOME_SHELL_CLIENT_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_GNOME_SHELL_BUS_NAME "org.gnome.Shell"

/* ShellActionMode stolen from gnome-shell */
/**
 * GPasteGnomeShellActionMode:
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_NONE: block action
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_NORMAL: allow action when in window mode,
 *     e.g. when the focus is in an application window
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_OVERVIEW: allow action while the overview
 *     is active
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_LOCK_SCREEN: allow action when the screen
 *     is locked, e.g. when the screen shield is shown
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_UNLOCK_SCREEN: allow action in the unlock
 *     dialog
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_LOGIN_SCREEN: allow action in the login screen
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_SYSTEM_MODAL: allow action when a system modal
 *     dialog (e.g. authentification or session dialogs) is open
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_LOOKING_GLASS: allow action in looking glass
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_POPUP: allow action while a shell menu is open
 * @G_PASTE_GNOME_SHELL_ACTION_MODE_ALL: always allow action
 *
 * Controls in which GNOME Shell states an action (like keybindings and gestures)
 * should be handled.
*/
typedef enum {
  G_PASTE_GNOME_SHELL_ACTION_MODE_NONE          = 0,
  G_PASTE_GNOME_SHELL_ACTION_MODE_NORMAL        = 1 << 0,
  G_PASTE_GNOME_SHELL_ACTION_MODE_OVERVIEW      = 1 << 1,
  G_PASTE_GNOME_SHELL_ACTION_MODE_LOCK_SCREEN   = 1 << 2,
  G_PASTE_GNOME_SHELL_ACTION_MODE_UNLOCK_SCREEN = 1 << 3,
  G_PASTE_GNOME_SHELL_ACTION_MODE_LOGIN_SCREEN  = 1 << 4,
  G_PASTE_GNOME_SHELL_ACTION_MODE_SYSTEM_MODAL  = 1 << 5,
  G_PASTE_GNOME_SHELL_ACTION_MODE_LOOKING_GLASS = 1 << 6,
  G_PASTE_GNOME_SHELL_ACTION_MODE_POPUP         = 1 << 7,

  G_PASTE_GNOME_SHELL_ACTION_MODE_ALL = ~0,
} GPasteGnomeShellActionMode;

#define G_PASTE_TYPE_GNOME_SHELL_CLIENT (g_paste_gnome_shell_client_get_type ())

typedef struct
{
    const gchar               *accelerator;
    GPasteGnomeShellActionMode flags;
} GPasteGnomeShellAccelerator;

#define G_PASTE_GNOME_SHELL_ACCELERATOR(accelerator) ((GPasteGnomeShellAccelerator) {accelerator, G_PASTE_GNOME_SHELL_ACTION_MODE_ALL})

G_PASTE_FINAL_TYPE (GnomeShellClient, gnome_shell_client, GNOME_SHELL_CLIENT, GDBusProxy)

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
