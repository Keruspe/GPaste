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

static void
count_invalidations (GPasteUiHistoryModel *model G_GNUC_UNUSED,
                     guint64               position G_GNUC_UNUSED,
                     guint64               n_items G_GNUC_UNUSED,
                     gpointer              user_data)
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
    guint invalidations = 0;

    g_paste_ui_history_model_set_size (model, 3);
    g_signal_connect (model, "invalidated", G_CALLBACK (count_invalidations), &invalidations);

    g_paste_ui_history_model_item_replaced (model, 3);
    g_paste_ui_history_model_item_replaced (model, G_MAXUINT64);
    g_paste_ui_history_model_invalidate (model, 2, G_MAXUINT64);
    g_assert_cmpuint (invalidations, ==, 0);

    g_paste_ui_history_model_invalidate (model, 1, 2);
    g_assert_cmpuint (invalidations, ==, 1);
}

/* Rows invalidated keep their items, which is what keeps their widgets -- and
 * the keyboard focus on one -- the list view being told nothing changed. */
static void
invalidate_keeps_items (void)
{
    g_autoptr (GPasteUiHistoryModel) model = g_paste_ui_history_model_new ();
    guint changes = 0;

    g_paste_ui_history_model_set_size (model, 3);

    GPasteUiHistoryItem *item = g_paste_ui_history_model_peek (model, 1);

    g_signal_connect (model, "items-changed", G_CALLBACK (count_changes), &changes);
    g_paste_ui_history_model_invalidate (model, 0, 3);
    g_assert_cmpuint (changes, ==, 0);
    g_assert_true (g_paste_ui_history_model_peek (model, 1) == item);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/ui/history-model/invalidate-out-of-range", invalidate_out_of_range);
    g_test_add_func ("/ui/history-model/invalidate-keeps-items", invalidate_keeps_items);
    return g_paste_test_env_run ();
}
