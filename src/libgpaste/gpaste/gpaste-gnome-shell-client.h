/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

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

/* MetaKeyBindingFlags stolen from mutter */
/**
 *  GPasteMetaKeyBindingFlags:
 *  @G_PASTE_META_KEY_BINDING_NONE: none
 *  @G_PASTE_META_KEY_BINDING_PER_WINDOW: per-window
 *  @G_PASTE_META_KEY_BINDING_BUILTIN: built-in
 *  @G_PASTE_META_KEY_BINDING_IS_REVERSED: is reversed
 *  @G_PASTE_META_KEY_BINDING_NON_MASKABLE: always active
 */
typedef enum
{
    G_PASTE_META_KEY_BINDING_NONE              = 0,
    G_PASTE_META_KEY_BINDING_PER_WINDOW        = 1 << 0,
    G_PASTE_META_KEY_BINDING_BUILTIN           = 1 << 1,
    G_PASTE_META_KEY_BINDING_IS_REVERSED       = 1 << 2,
    G_PASTE_META_KEY_BINDING_NON_MASKABLE      = 1 << 3,
    G_PASTE_META_KEY_BINDING_IGNORE_AUTOREPEAT = 1 << 4,
} GPasteMetaKeyBindingFlags;

#define G_PASTE_TYPE_GNOME_SHELL_CLIENT (g_paste_gnome_shell_client_get_type ())

typedef struct
{
    const gchar               *accelerator;
    GPasteMetaKeyBindingFlags  mode_flags;
    GPasteGnomeShellActionMode grab_flags;
} GPasteGnomeShellAccelerator;

#define G_PASTE_GNOME_SHELL_ACCELERATOR(accelerator) ((GPasteGnomeShellAccelerator) {accelerator, G_PASTE_META_KEY_BINDING_NONE, G_PASTE_GNOME_SHELL_ACTION_MODE_ALL})

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
