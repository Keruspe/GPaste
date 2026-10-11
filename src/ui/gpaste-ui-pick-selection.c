// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-pick-selection.h>

/* The history list's one selection model: a #GtkMultiSelection while items are
 * being picked, and nothing selectable the rest of the time. One model rather
 * than a #GtkNoSelection swapped for a #GtkMultiSelection and back: a
 * #GtkListView handed another model goes back to its top, builds every row anew
 * and drops the keyboard focus
 * (/ui/daemon-presence/selection-mode-keeps-place). */
struct _GPasteUiPickSelection
{
    GObject parent_instance;

    GtkSelectionModel *picks;
    gboolean           picking;
};

static void g_paste_ui_pick_selection_list_model_iface_init      (GListModelInterface        *iface);
static void g_paste_ui_pick_selection_selection_model_iface_init (GtkSelectionModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GPasteUiPickSelection, g_paste_ui_pick_selection, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, g_paste_ui_pick_selection_list_model_iface_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_SELECTION_MODEL, g_paste_ui_pick_selection_selection_model_iface_init))

static GType
g_paste_ui_pick_selection_get_item_type (GListModel *list)
{
    return g_list_model_get_item_type (G_LIST_MODEL (G_PASTE_UI_PICK_SELECTION (list)->picks));
}

static guint
g_paste_ui_pick_selection_get_n_items (GListModel *list)
{
    return g_list_model_get_n_items (G_LIST_MODEL (G_PASTE_UI_PICK_SELECTION (list)->picks));
}

static gpointer
g_paste_ui_pick_selection_get_item (GListModel *list,
                                    guint       position)
{
    return g_list_model_get_item (G_LIST_MODEL (G_PASTE_UI_PICK_SELECTION (list)->picks), position);
}

static void
g_paste_ui_pick_selection_list_model_iface_init (GListModelInterface *iface)
{
    iface->get_item_type = g_paste_ui_pick_selection_get_item_type;
    iface->get_n_items = g_paste_ui_pick_selection_get_n_items;
    iface->get_item = g_paste_ui_pick_selection_get_item;
}

/* Every question and request goes to the picks while picking, and finds
 * nothing selected nor selectable otherwise. */
#define PICKS(model) ((G_PASTE_UI_PICK_SELECTION (model)->picking) ? G_PASTE_UI_PICK_SELECTION (model)->picks : NULL)

static gboolean
g_paste_ui_pick_selection_is_selected (GtkSelectionModel *model,
                                       guint              position)
{
    return PICKS (model) && gtk_selection_model_is_selected (PICKS (model), position);
}

static GtkBitset *
g_paste_ui_pick_selection_get_selection_in_range (GtkSelectionModel *model,
                                                  guint              position,
                                                  guint              n_items)
{
    return (PICKS (model)) ? gtk_selection_model_get_selection_in_range (PICKS (model), position, n_items) : gtk_bitset_new_empty ();
}

static gboolean
g_paste_ui_pick_selection_select_item (GtkSelectionModel *model,
                                       guint              position,
                                       gboolean           unselect_rest)
{
    return PICKS (model) && gtk_selection_model_select_item (PICKS (model), position, unselect_rest);
}

static gboolean
g_paste_ui_pick_selection_unselect_item (GtkSelectionModel *model,
                                         guint              position)
{
    return PICKS (model) && gtk_selection_model_unselect_item (PICKS (model), position);
}

static gboolean
g_paste_ui_pick_selection_select_range (GtkSelectionModel *model,
                                        guint              position,
                                        guint              n_items,
                                        gboolean           unselect_rest)
{
    return PICKS (model) && gtk_selection_model_select_range (PICKS (model), position, n_items, unselect_rest);
}

static gboolean
g_paste_ui_pick_selection_unselect_range (GtkSelectionModel *model,
                                          guint              position,
                                          guint              n_items)
{
    return PICKS (model) && gtk_selection_model_unselect_range (PICKS (model), position, n_items);
}

static gboolean
g_paste_ui_pick_selection_select_all (GtkSelectionModel *model)
{
    return PICKS (model) && gtk_selection_model_select_all (PICKS (model));
}

static gboolean
g_paste_ui_pick_selection_unselect_all (GtkSelectionModel *model)
{
    return PICKS (model) && gtk_selection_model_unselect_all (PICKS (model));
}

static gboolean
g_paste_ui_pick_selection_set_selection (GtkSelectionModel *model,
                                         GtkBitset         *selected,
                                         GtkBitset         *mask)
{
    return PICKS (model) && gtk_selection_model_set_selection (PICKS (model), selected, mask);
}

static void
g_paste_ui_pick_selection_selection_model_iface_init (GtkSelectionModelInterface *iface)
{
    iface->is_selected = g_paste_ui_pick_selection_is_selected;
    iface->get_selection_in_range = g_paste_ui_pick_selection_get_selection_in_range;
    iface->select_item = g_paste_ui_pick_selection_select_item;
    iface->unselect_item = g_paste_ui_pick_selection_unselect_item;
    iface->select_range = g_paste_ui_pick_selection_select_range;
    iface->unselect_range = g_paste_ui_pick_selection_unselect_range;
    iface->select_all = g_paste_ui_pick_selection_select_all;
    iface->unselect_all = g_paste_ui_pick_selection_unselect_all;
    iface->set_selection = g_paste_ui_pick_selection_set_selection;
}

static void
on_items_changed (GListModel *model G_GNUC_UNUSED,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
    g_list_model_items_changed (user_data, position, removed, added);
}

static void
on_selection_changed (GtkSelectionModel *model G_GNUC_UNUSED,
                      guint              position,
                      guint              n_items,
                      gpointer           user_data)
{
    GPasteUiPickSelection *self = user_data;

    if (self->picking)
        gtk_selection_model_selection_changed (GTK_SELECTION_MODEL (self), position, n_items);
}

/**
 * g_paste_ui_pick_selection_set_picking:
 * @self: a #GPasteUiPickSelection
 * @picking: whether items can be picked
 *
 * Start picking, or stop. Either way, nothing is picked yet: the picks of a
 * mode left go with it.
 */
void
g_paste_ui_pick_selection_set_picking (GPasteUiPickSelection *self,
                                       gboolean               picking)
{
    g_return_if_fail (G_PASTE_IS_UI_PICK_SELECTION (self));

    /* While still picking, so that the rows drawn as picked hear of it. */
    gtk_selection_model_unselect_all (self->picks);
    self->picking = picking;
}

static void
g_paste_ui_pick_selection_dispose (GObject *object)
{
    GPasteUiPickSelection *self = G_PASTE_UI_PICK_SELECTION (object);

    if (self->picks)
        g_signal_handlers_disconnect_by_data (self->picks, self);
    g_clear_object (&self->picks);

    G_OBJECT_CLASS (g_paste_ui_pick_selection_parent_class)->dispose (object);
}

static void
g_paste_ui_pick_selection_class_init (GPasteUiPickSelectionClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_pick_selection_dispose;
}

static void
g_paste_ui_pick_selection_init (GPasteUiPickSelection *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_pick_selection_new:
 * @model: (transfer none): the rows to pick from
 *
 * Create a new #GPasteUiPickSelection over @model, not picking.
 *
 * Returns: (transfer full): a newly allocated #GPasteUiPickSelection
 */
GPasteUiPickSelection *
g_paste_ui_pick_selection_new (GListModel *model)
{
    g_return_val_if_fail (G_IS_LIST_MODEL (model), NULL);

    GPasteUiPickSelection *self = g_object_new (G_PASTE_TYPE_UI_PICK_SELECTION, NULL);

    self->picks = GTK_SELECTION_MODEL (gtk_multi_selection_new (g_object_ref (model)));
    g_signal_connect (self->picks, "items-changed", G_CALLBACK (on_items_changed), self);
    g_signal_connect (self->picks, "selection-changed", G_CALLBACK (on_selection_changed), self);

    return self;
}
