// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-color-swatch.h>

/* What a colour item looks like, next to its row: a small square of the colour
 * it holds, outlined so that a colour close to what the theme paints behind it
 * is still a swatch rather than a hole.
 *
 * Nothing here draws anything. The colour is one pixel of texture stretched
 * over the widget (GTK_CONTENT_FIT_FILL), and the outline is Adwaita's own
 * ".frame" -- "1px solid currentColor at the border opacity", which is exactly
 * what a hand-drawn border would have to work out for itself, except that the
 * theme keeps it right when the theme changes. Overflow is clipped so the
 * colour stays inside that frame whatever radius a theme gives it.
 *
 * A GtkPicture rather than a widget of our own, and a texture rather than CSS,
 * because both alternatives cost more than they give: GTK4 has no supported way
 * to give one widget its own colour (providers are per-display, and the
 * per-widget pair has been deprecated since 4.10), and drawing it ourselves
 * would mean a snapshot, a rounded rect and a border colour to keep in step with
 * the theme by hand.
 */

/* Side of the swatch, in pixels. A constant, not images-preview-size: that
 * setting sizes image thumbnails, and turning image previews off must not take
 * the colours with it. */
#define G_PASTE_UI_COLOR_SWATCH_SIZE 24

/**
 * g_paste_ui_color_swatch_set_color:
 * @self: the #GtkPicture g_paste_ui_color_swatch_new() made
 * @color: (nullable): the colour to show, as gdk_rgba_to_string() writes it
 *
 * Show @color, hiding the swatch when @color is %NULL or GDK cannot read it
 * back — a swatch of the wrong colour would be worse than none.
 *
 * Returns: whether there is now a colour to show
 */
gboolean
g_paste_ui_color_swatch_set_color (GtkWidget   *self,
                                   const gchar *color)
{
    g_return_val_if_fail (GTK_IS_PICTURE (self), FALSE);

    GdkRGBA rgba;

    if (!color || !gdk_rgba_parse (&rgba, color))
    {
        gtk_picture_set_paintable (GTK_PICTURE (self), NULL);
        gtk_widget_set_visible (self, FALSE);

        return FALSE;
    }

    /* Straight (non-premultiplied) RGBA, so a translucent colour composites over
     * the row exactly as the clipboard held it. */
    const guint8 pixel[] = {
        (guint8) (CLAMP (rgba.red,   0.f, 1.f) * 255.f + 0.5f),
        (guint8) (CLAMP (rgba.green, 0.f, 1.f) * 255.f + 0.5f),
        (guint8) (CLAMP (rgba.blue,  0.f, 1.f) * 255.f + 0.5f),
        (guint8) (CLAMP (rgba.alpha, 0.f, 1.f) * 255.f + 0.5f),
    };
    g_autoptr (GBytes) bytes = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, sizeof (pixel));

    gtk_picture_set_paintable (GTK_PICTURE (self), GDK_PAINTABLE (texture));
    gtk_widget_set_visible (self, TRUE);

    return TRUE;
}

/**
 * g_paste_ui_color_swatch_new:
 *
 * Create the swatch a colour item is shown with, hidden until it is given a
 * colour to show
 *
 * Returns: a newly allocated #GtkPicture
 *          free it with g_object_unref
 */
GtkWidget *
g_paste_ui_color_swatch_new (void)
{
    GtkWidget *self = gtk_picture_new ();

    /* One pixel stretched over the whole widget, rather than fitted into it. */
    gtk_picture_set_content_fit (GTK_PICTURE (self), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_size_request (self, G_PASTE_UI_COLOR_SWATCH_SIZE, G_PASTE_UI_COLOR_SWATCH_SIZE);
    gtk_widget_set_halign (self, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (self, "frame");
    gtk_widget_set_overflow (self, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_visible (self, FALSE);

    return self;
}
