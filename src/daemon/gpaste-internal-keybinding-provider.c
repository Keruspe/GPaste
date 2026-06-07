// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-internal-keybinding-provider.h>

#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/wayland/gdkwayland.h>
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
#  include <gdk/x11/gdkx.h>
#  include <X11/extensions/XInput2.h>
#endif

typedef struct
{
    gchar          *id;
    guint32        *keycodes;   /* NULL-terminated, owned */
    GdkModifierType modifiers;
} _InternalBinding;

static void
_internal_binding_free (gpointer data)
{
    g_autofree _InternalBinding *b = data;
    g_free (b->id);
    g_free (b->keycodes);
}

typedef struct
{
    GPtrArray *bindings;  /* _InternalBinding*, freed with _internal_binding_free */
    guint64    c_xevent;
} GPasteInternalKeybindingProviderPrivate;

struct _GPasteInternalKeybindingProvider
{
    GObject parent_instance;
};

static void internal_provider_iface_init (GPasteKeybindingProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (InternalKeybindingProvider, internal_keybinding_provider, G_TYPE_OBJECT,
    G_PASTE_TYPE_KEYBINDING_PROVIDER, internal_provider_iface_init)

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
static gint
internal_provider_get_xinput_opcode (Display *display)
{
    static gint32 xinput_opcode = 0;

    if (!xinput_opcode)
    {
        gint32 major = 2, minor = 3;
        gint32 xinput_error_base;
        gint32 xinput_event_base;

        if (XQueryExtension (display,
                             "XInputExtension",
                             &xinput_opcode,
                             &xinput_error_base,
                             &xinput_event_base))
        {
            if (XIQueryVersion (display, &major, &minor) != Success)
                g_warning ("XInput 2 not found, keybinder won't work");
        }
    }

    return xinput_opcode;
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static void
internal_provider_change_grab_x11 (const guint32  *keycodes,
                                   GdkModifierType modifiers,
                                   GdkDisplay     *display,
                                   gboolean        grab)
{
    Display *xdisplay = GDK_DISPLAY_XDISPLAY (display);
    Window window = gdk_x11_display_get_xrootwindow (display);

    /* Grab with every combination of the "lock" modifiers (NumLock, CapsLock)
     * so the shortcut fires regardless of their state. */
    static const guint locked_mods[] = {
        0,
        Mod2Mask /* NumLock */,
        LockMask /* CapsLock */,
        Mod2Mask | LockMask,
    };
    g_autoptr (GArray) mods = g_array_new (FALSE, TRUE, sizeof (XIGrabModifiers));

    guchar mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    if (modifiers & XIAnyModifier)
        g_array_append_val (mods, ((XIGrabModifiers) { XIAnyModifier, 0 }));
    else
    {
        for (guint64 i = 0; i < G_N_ELEMENTS (locked_mods); ++i)
            g_array_append_val (mods, ((XIGrabModifiers) { modifiers | locked_mods[i], 0 }));
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

static void
internal_provider_change_grab (const guint32  *keycodes,
                               GdkModifierType modifiers,
                               gboolean        grab)
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

/*****************************/
/* GPasteKeybindingProvider  */
/*****************************/

static void
internal_provider_ungrab_all (GPasteKeybindingProvider *provider)
{
    GPasteInternalKeybindingProvider *self = G_PASTE_INTERNAL_KEYBINDING_PROVIDER (provider);
    GPasteInternalKeybindingProviderPrivate *priv =
        g_paste_internal_keybinding_provider_get_instance_private (self);

    for (guint i = 0; i < priv->bindings->len; i++)
    {
        _InternalBinding *b = g_ptr_array_index (priv->bindings, i);
        internal_provider_change_grab (b->keycodes, b->modifiers, FALSE);
    }

    g_ptr_array_set_size (priv->bindings, 0);
}

static void
internal_provider_grab_all (GPasteKeybindingProvider          *provider,
                            const GPasteKeybindingAccelerator *accels)
{
    GPasteInternalKeybindingProvider *self = G_PASTE_INTERNAL_KEYBINDING_PROVIDER (provider);
    GPasteInternalKeybindingProviderPrivate *priv =
        g_paste_internal_keybinding_provider_get_instance_private (self);

    internal_provider_ungrab_all (provider);

    for (const GPasteKeybindingAccelerator *a = accels; a->id; a++)
    {
        guint32 *keycodes = NULL;
        GdkModifierType modifiers = 0;

        gtk_accelerator_parse_with_keycode (a->accelerator, NULL, NULL, &keycodes, &modifiers);

        if (!keycodes)
            continue;

        _InternalBinding *b = g_new (_InternalBinding, 1);
        b->id = g_strdup (a->id);
        b->keycodes = keycodes;
        b->modifiers = modifiers;

        g_ptr_array_add (priv->bindings, b);
        internal_provider_change_grab (keycodes, modifiers, TRUE);
    }
}

static void
internal_provider_iface_init (GPasteKeybindingProviderInterface *iface)
{
    iface->grab_all   = internal_provider_grab_all;
    iface->ungrab_all = internal_provider_ungrab_all;
}

/******************************/
/* XEvent dispatch            */
/******************************/

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
static void
internal_provider_parse_event_x11 (XEvent          *event,
                                   GdkModifierType *modifiers,
                                   guint64         *keycode)
{
    XGenericEventCookie cookie = event->xcookie;

    if (cookie.extension == internal_provider_get_xinput_opcode (NULL))
    {
        XIDeviceEvent *xi_ev = (XIDeviceEvent *) cookie.data;

        if (xi_ev->evtype == XI_KeyPress)
        {
            *modifiers = xi_ev->mods.effective;
            *keycode = xi_ev->detail;
        }
    }
}
#endif

static gboolean
internal_provider_xevent (GdkDisplay *display,
                          XEvent     *xevent,
                          gpointer    data)
{
    GPasteInternalKeybindingProvider *self = data;
    GPasteInternalKeybindingProviderPrivate *priv =
        g_paste_internal_keybinding_provider_get_instance_private (self);
    GdkModifierType modifiers = 0;
    guint64 keycode = 0;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_warning ("Wayland hotkeys are currently not supported outside of gnome-shell.");
    else
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY (display))
        internal_provider_parse_event_x11 (xevent, &modifiers, &keycode);
    else
#endif
        g_warning ("Unsupported GDK backend, keybinder won't work.");

    if (!keycode)
        return FALSE;

    for (guint i = 0; i < priv->bindings->len; i++)
    {
        _InternalBinding *b = g_ptr_array_index (priv->bindings, i);

        if (!b->keycodes)
            continue;

        if (b->modifiers != (b->modifiers & modifiers))
            continue;

        for (const guint32 *kc = b->keycodes; *kc; ++kc)
        {
            if (keycode == *kc)
            {
                g_paste_keybinding_provider_emit_keybinding_activated (G_PASTE_KEYBINDING_PROVIDER (self), b->id);
                break;
            }
        }
    }

    return FALSE;
}

/************************/
/* GObject boilerplate  */
/************************/

static void
g_paste_internal_keybinding_provider_dispose (GObject *object)
{
    GPasteInternalKeybindingProvider *self = G_PASTE_INTERNAL_KEYBINDING_PROVIDER (object);
    GPasteInternalKeybindingProviderPrivate *priv =
        g_paste_internal_keybinding_provider_get_instance_private (self);

    if (priv->c_xevent)
    {
        g_signal_handler_disconnect (gdk_display_get_default (), priv->c_xevent);
        priv->c_xevent = 0;
    }

    if (priv->bindings)
    {
        internal_provider_ungrab_all (G_PASTE_KEYBINDING_PROVIDER (self));
        g_clear_pointer (&priv->bindings, g_ptr_array_unref);
    }

    G_OBJECT_CLASS (g_paste_internal_keybinding_provider_parent_class)->dispose (object);
}

static void
g_paste_internal_keybinding_provider_class_init (GPasteInternalKeybindingProviderClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_internal_keybinding_provider_dispose;
}

static void
g_paste_internal_keybinding_provider_init (GPasteInternalKeybindingProvider *self)
{
    GPasteInternalKeybindingProviderPrivate *priv =
        g_paste_internal_keybinding_provider_get_instance_private (self);

    priv->bindings = g_ptr_array_new_with_free_func (_internal_binding_free);
    priv->c_xevent = 0;

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    GdkDisplay *display = gdk_display_get_default ();

    if (GDK_IS_X11_DISPLAY (display))
    {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        internal_provider_get_xinput_opcode (GDK_DISPLAY_XDISPLAY (display));
        G_GNUC_END_IGNORE_DEPRECATIONS

        priv->c_xevent = g_signal_connect (display,
                                           "xevent",
                                           G_CALLBACK (internal_provider_xevent),
                                           self);
    }
#endif
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
