/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>

#include <gpaste-upload-keybinding.h>

struct _GPasteUploadKeybinding
{
    GPasteKeybinding parent_instance;
};

typedef struct
{
    GPasteDaemon *daemon;
} GPasteUploadKeybindingPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UploadKeybinding, upload_keybinding, G_PASTE_TYPE_KEYBINDING)

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
    const GPasteUploadKeybindingPrivate *priv = _g_paste_upload_keybinding_get_instance_private (G_PASTE_UPLOAD_KEYBINDING (self));

    g_paste_daemon_upload (priv->daemon, NULL);
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
    g_return_val_if_fail (_G_PASTE_IS_DAEMON (daemon), NULL);

    GPasteKeybinding *self = g_paste_keybinding_new (G_PASTE_TYPE_UPLOAD_KEYBINDING,
                                                     G_PASTE_UPLOAD_SETTING,
                                                     g_paste_settings_get_upload,
                                                     upload,
                                                     NULL);
    GPasteUploadKeybindingPrivate *priv = g_paste_upload_keybinding_get_instance_private (G_PASTE_UPLOAD_KEYBINDING (self));

    priv->daemon = g_object_ref (daemon);

    return self;
}
