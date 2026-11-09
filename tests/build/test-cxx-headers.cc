// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The installed umbrella headers, as a C++ consumer includes them: without
 * G_PASTE_COMPILATION, which only GPaste's own C sources are built with. That
 * this compiles is the test, the inline helpers the headers carry included. */

#include <gpaste.h>
#include <gpaste-gtk4.h>
#include <gpaste-daemon.h>

int
main ()
{
    GCancellable *cancellable = g_cancellable_new ();
    GObject *object = G_OBJECT (g_object_new (G_TYPE_OBJECT, NULL));

    g_paste_weak_ref_free (g_paste_weak_ref_new (object));
    g_object_unref (object);
    g_paste_clear_cancellable (&cancellable);

    return (g_paste_str_equal ("a", "a") && !cancellable) ? 0 : 1;
}
