// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-ui-history-model.h>

static void
count_changes (GListModel *model G_GNUC_UNUSED,
               guint       position G_GNUC_UNUSED,
               guint       removed G_GNUC_UNUSED,
               guint       added G_GNUC_UNUSED,
               gpointer    user_data)
{
    ++*(guint *) user_data;
}

/* A range past the rows is none of the model's, however far past: an update's
 * position comes from the daemon, and one near the top of its range must not
 * wrap around into the rows. */
static void
invalidate_out_of_range (void)
{
    g_autoptr (GPasteUiHistoryModel) model = g_paste_ui_history_model_new ();
    guint changes = 0;

    g_paste_ui_history_model_set_size (model, 3);
    g_signal_connect (model, "items-changed", G_CALLBACK (count_changes), &changes);

    g_paste_ui_history_model_item_replaced (model, 3);
    g_paste_ui_history_model_item_replaced (model, G_MAXUINT64);
    g_paste_ui_history_model_invalidate (model, 2, G_MAXUINT64);
    g_assert_cmpuint (changes, ==, 0);

    g_paste_ui_history_model_invalidate (model, 1, 2);
    g_assert_cmpuint (changes, ==, 1);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/ui/history-model/invalidate-out-of-range", invalidate_out_of_range);
    return g_paste_test_env_run ();
}
