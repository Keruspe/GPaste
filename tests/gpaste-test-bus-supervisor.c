// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <unistd.h>

/* stdin is a lifeline held by the test process, which writes on it to stop us
 * and otherwise ends it by exiting: EOF survives an exec's security-domain
 * transition, which can clear PR_SET_PDEATHSIG on dbus-daemon. The server
 * inherits our output pipes, so it must be reaped before we exit.
 * The directories the test runs in go with it: a test that crashes never
 * reaches the removal of its own. A signal sent to the whole group, as a test
 * runner's timeout does, is left to the lifeline for the same reason. */
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
    if (self->bus)
        g_subprocess_force_exit (self->bus);
    else
        g_main_loop_quit (self->loop);
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

/* Nothing but the tests writes under these directories, but a symlink there
 * must still not lead the removal out of them. */
static void
remove_tree (GFile *file)
{
    if (g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL) == G_FILE_TYPE_DIRECTORY)
    {
        g_autoptr (GFileEnumerator) children = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                                                          NULL, NULL);
        GFile *child;

        while (children && g_file_enumerator_iterate (children, NULL, &child, NULL, NULL) && child)
            remove_tree (child);
    }
    g_file_delete (file, NULL, NULL);
}

/* Infrastructure helper, not a test binary: creating another test environment
 * here would recursively launch another supervisor. */
int
main (int argc, char **argv)
{
    /* The bus configuration, empty for no bus, then the directories to remove. */
    if (argc < 2)
        return 2;

    g_autoptr (GError) error = NULL;
    g_autoptr (GSubprocess) bus = NULL;

    if (*argv[1])
    {
        bus = g_subprocess_new (G_SUBPROCESS_FLAGS_NONE, &error,
                                "dbus-daemon", "--nofork", "--print-address=1",
                                "--config-file", argv[1], NULL);
        if (!bus)
            g_printerr ("Could not start the test bus: %s\n", error->message);
    }

    /* A bus that never started leaves the test dying on the address it waits
     * for, so its directories go now. */
    gboolean failed = *argv[1] && !bus;

    if (!failed)
    {
        g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
        Supervisor self = { .bus = bus, .loop = loop };

        signal (SIGTERM, SIG_IGN);
        signal (SIGINT, SIG_IGN);

        self.parent_watch = g_unix_fd_add (STDIN_FILENO, G_IO_IN | G_IO_HUP | G_IO_ERR, parent_gone, &self);
        g_source_set_name_by_id (self.parent_watch, "[GPaste] test lifeline");
        if (bus)
            g_subprocess_wait_async (bus, NULL, bus_exited, &self);
        g_main_loop_run (loop);
        g_clear_handle_id (&self.parent_watch, g_source_remove);
    }

    for (gint i = 2; i < argc; ++i)
    {
        g_autoptr (GFile) directory = g_file_new_for_path (argv[i]);

        remove_tree (directory);
    }
    return failed;
}
