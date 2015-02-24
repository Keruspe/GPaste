/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-keybinding-private.h"

#include <gpaste-gsettings-keys.h>
#include <gpaste-upload-keybinding.h>

struct _GPasteUploadKeybinding
{
    GPasteKeybinding parent_instance;
};

typedef struct
{
    GPasteDaemon *daemon;
} GPasteUploadKeybindingPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUploadKeybinding, g_paste_upload_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_upload_keybinding_dispose (GObject *object)
{
    GPasteUploadKeybindingPrivate *priv = g_paste_upload_keybinding_get_instance_private (G_PASTE_UPLOAD_KEYBINDING (object));

    g_clear_object (&priv->daemon);

    G_OBJECT_CLASS (g_paste_upload_keybinding_parent_class)->dispose (object);
}

static void
g_paste_upload_keybinding_class_init (GPasteUploadKeybindingClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_upload_keybinding_dispose;
}

static void
g_paste_upload_keybinding_init (GPasteUploadKeybinding *self G_GNUC_UNUSED)
{
}

static void
upload (GPasteKeybinding *self,
        gpointer          data G_GNUC_UNUSED)
{
    GPasteUploadKeybindingPrivate *priv = g_paste_upload_keybinding_get_instance_private (G_PASTE_UPLOAD_KEYBINDING (self));

    g_paste_daemon_upload (priv->daemon, 0);
}

/**
 * g_paste_upload_keybinding_new:
 * @daemon: a #GPasteDaemon instance
 *
 * Create a new instance of #GPasteUploadKeybinding
 *
 * Returns: a newly allocated #GPasteUploadKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_upload_keybinding_new (GPasteDaemon *daemon)
{
    g_return_val_if_fail (G_PASTE_IS_DAEMON (daemon), NULL);

    GPasteKeybinding *self = _g_paste_keybinding_new (G_PASTE_TYPE_UPLOAD_KEYBINDING,
                                                      G_PASTE_UPLOAD_SETTING,
                                                      g_paste_settings_get_upload,
                                                      upload,
                                                      NULL);
    GPasteUploadKeybindingPrivate *priv = g_paste_upload_keybinding_get_instance_private (G_PASTE_UPLOAD_KEYBINDING (self));

    priv->daemon = g_object_ref (daemon);

    return self;
}
