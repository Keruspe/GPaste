// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gio/gio.h>
#include <glib-unix.h>
#include <unistd.h>

/* stdin is a lifeline held only by the test process. EOF survives an exec's
 * security-domain transition, which can clear PR_SET_PDEATHSIG on dbus-daemon.
 * The server inherits our output pipes, so it must be reaped before we exit. */
typedef struct
{
    GSubprocess *bus;
    GMainLoop   *loop;
    guint        parent_watch;
} Supervisor;

static gboolean
parent_gone (gint         fd G_GNUC_UNUSED,
             GIOCondition condition G_GNUC_UNUSED,
             gpointer     user_data)
{
    Supervisor *self = user_data;

    self->parent_watch = 0;
    g_subprocess_force_exit (self->bus);
    return G_SOURCE_REMOVE;
}

static void
bus_exited (GObject      *source,
            GAsyncResult *result,
            gpointer      user_data)
{
    Supervisor *self = user_data;

    g_subprocess_wait_finish (G_SUBPROCESS (source), result, NULL);
    g_main_loop_quit (self->loop);
}

/* Infrastructure helper, not a test binary: creating another test environment
 * here would recursively launch another supervisor. */
int
main (int argc, char **argv)
{
    if (argc != 2)
        return 2;

    g_autoptr (GError) error = NULL;
    g_autoptr (GSubprocess) bus = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error,
                                                   "dbus-daemon", "--nofork", "--print-address=1",
                                                   "--config-file", argv[1], NULL);
    if (!bus)
    {
        g_printerr ("Could not start the test bus: %s\n", error->message);
        return 1;
    }

    g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
    Supervisor self = { .bus = bus, .loop = loop };

    self.parent_watch = g_unix_fd_add (STDIN_FILENO, G_IO_IN | G_IO_HUP | G_IO_ERR, parent_gone, &self);
    g_subprocess_wait_async (bus, NULL, bus_exited, &self);
    g_main_loop_run (loop);
    g_clear_handle_id (&self.parent_watch, g_source_remove);
    return 0;
}
