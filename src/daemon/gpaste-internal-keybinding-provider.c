/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-internal-keybinding-provider.h>

#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/wayland/gdkwayland.h>
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
#  include <gdk/x11/gdkx.h>
#  include <X11/extensions/XInput2.h>
#endif

struct _GPasteInternalKeybindingProvider
{
    GObject parent_instance;
};

G_PASTE_DEFINE_TYPE (InternalKeybindingProvider, internal_keybinding_provider, G_TYPE_OBJECT)

/****************************/
/* Platform-specific grabbing */
/****************************/

#ifdef GDK_WINDOWING_WAYLAND
static void
internal_provider_change_grab_wayland (void)
{
    g_warning_once ("Wayland hotkeys are currently not supported outside of gnome-shell.");
}
#endif

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
internal_provider_change_grab_x11 (const guint32  *keycodes,
                                   GdkModifierType modifiers,
                                   GdkDisplay     *display,
                                   gboolean        grab)
{
    Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);
    Window window = gdk_x11_display_get_xrootwindow (display);

    guint64 mod_masks = Mod2Mask /* NumLock */ | LockMask /* CapsLock */;
    g_autoptr (GArray) mods = g_array_new (FALSE, TRUE, sizeof (XIGrabModifiers));

    guchar mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    if (modifiers & XIAnyModifier)
    {
        g_array_append_val (mods, ((XIGrabModifiers) { XIAnyModifier, 0 }));
    }
    else
    {
        g_array_append_val (mods, ((XIGrabModifiers) { modifiers, 0 }));

        for (guint64 i = 0; i < mod_masks; ++i) {
            if (i & mod_masks)
                g_array_append_val (mods, ((XIGrabModifiers) { modifiers | i, 0 }));
        }
    }

    XISetMask (mask.mask, XI_KeyPress);

    gdk_x11_display_error_trap_push (display);

    for (const guint32 *keycode = keycodes; *keycode; ++keycode)
    {
        if (grab)
        {
            XIGrabKeycode (xdisplay,
                           XIAllMasterDevices,
                           *keycode,
                           window,
                           XIGrabModeSync,
                           XIGrabModeAsync,
                           False,
                           &mask,
                           mods->len,
                           (XIGrabModifiers *) mods->data);
        }
        else
        {
            XIUngrabKeycode (xdisplay,
                             XIAllMasterDevices,
                             *keycode,
                             window,
                             mods->len,
                             (XIGrabModifiers *) mods->data);
        }
    }

    gdk_display_flush (display);
    gdk_x11_display_error_trap_pop_ignored (display);
}
G_GNUC_END_IGNORE_DEPRECATIONS
#endif

void
g_paste_internal_keybinding_provider_change_grab (GPasteInternalKeybindingProvider *self G_GNUC_UNUSED,
                                                   const guint32                    *keycodes,
                                                   GdkModifierType                   modifiers,
                                                   gboolean                          grab)
{
    GdkDisplay *display = gdk_display_get_default ();

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        internal_provider_change_grab_wayland ();
    else
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY (display))
        internal_provider_change_grab_x11 (keycodes, modifiers, display, grab);
    else
#endif
        g_warning ("Unsupported GDK backend, keybinder won't work.");
}

static void
g_paste_internal_keybinding_provider_class_init (GPasteInternalKeybindingProviderClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_internal_keybinding_provider_init (GPasteInternalKeybindingProvider *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_internal_keybinding_provider_new:
 *
 * Create a new instance of #GPasteInternalKeybindingProvider
 *
 * Returns: a newly allocated #GPasteInternalKeybindingProvider
 *          free it with g_object_unref
 */
GPasteInternalKeybindingProvider *
g_paste_internal_keybinding_provider_new (void)
{
    return g_object_new (G_PASTE_TYPE_INTERNAL_KEYBINDING_PROVIDER, NULL);
}
