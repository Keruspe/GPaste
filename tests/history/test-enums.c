// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-client.h>
#include <gpaste-3/gpaste-storage.h>
#include <gpaste-3/gpaste-update-enums.h>
#include <gpaste-daemon/gpaste-special-mime.h>
#include <gpaste-daemon/gpaste-prompt.h>
#include <dlfcn.h>

/* Widen the first-registration window without replacing GLib's registration:
 * concurrent callers must wait for the same published type. */
GType
g_enum_register_static (const gchar      *name,
                        const GEnumValue *values)
{
    typedef GType (*Register) (const gchar *, const GEnumValue *);
    Register original = (Register) dlsym (RTLD_NEXT, "g_enum_register_static");

    g_assert_nonnull (original);
    if (g_str_has_prefix (name, "GPaste"))
        g_usleep (10000);
    return original (name, values);
}

typedef struct
{
    GType      (*get_type) (void);
    const gchar *name;
} EnumCase;

typedef struct
{
    GMutex          mutex;
    GCond           cond;
    guint           ready;
    gboolean        start;
    const EnumCase *test;
} Registration;

static gpointer
register_enum (gpointer data)
{
    Registration *registration = data;

    g_mutex_lock (&registration->mutex);
    ++registration->ready;
    g_cond_broadcast (&registration->cond);
    while (!registration->start)
        g_cond_wait (&registration->cond, &registration->mutex);
    g_mutex_unlock (&registration->mutex);

    GType type = registration->test->get_type ();

    g_assert_cmpuint (type, !=, G_TYPE_INVALID);
    g_assert_cmpstr (g_type_name (type), ==, registration->test->name);
    return GSIZE_TO_POINTER (type);
}

static void
test_enum (gconstpointer data)
{
    /* A fresh process for each type, so that its first use is the one racing. */
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    Registration registration = { .test = data };
    GThread *threads[32] = { 0 };

    g_mutex_init (&registration.mutex);
    g_cond_init (&registration.cond);
    for (guint i = 0; i < G_N_ELEMENTS (threads); ++i)
        threads[i] = g_thread_new ("enum-registration", register_enum, &registration);

    g_mutex_lock (&registration.mutex);
    while (registration.ready != G_N_ELEMENTS (threads))
        g_cond_wait (&registration.cond, &registration.mutex);
    registration.start = TRUE;
    g_cond_broadcast (&registration.cond);
    g_mutex_unlock (&registration.mutex);

    GType expected = registration.test->get_type ();

    for (guint i = 0; i < G_N_ELEMENTS (threads); ++i)
        g_assert_cmpuint (GPOINTER_TO_SIZE (g_thread_join (g_steal_pointer (&threads[i]))), ==, expected);

    g_cond_clear (&registration.cond);
    g_mutex_clear (&registration.mutex);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    static const EnumCase cases[] = {
        { g_paste_item_kind_get_type,       "GPasteItemKind"       },
        { g_paste_storage_get_type,         "GPasteStorage"        },
        { g_paste_update_action_get_type,   "GPasteUpdateAction"   },
        { g_paste_update_target_get_type,   "GPasteUpdateTarget"   },
        { g_paste_daemon_presence_get_type, "GPasteDaemonPresence" },
        { g_paste_special_mime_get_type,    "GPasteSpecialMime"    },
        { g_paste_prompt_text_get_type,     "GPastePromptText"     },
    };

    for (guint i = 0; i < G_N_ELEMENTS (cases); ++i)
    {
        g_autofree gchar *path = g_strdup_printf ("/enums/%s", cases[i].name);

        g_test_add_data_func (path, &cases[i], test_enum);
    }
    return g_paste_test_env_run ();
}
