/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2019, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

const PopupMenu = imports.ui.popupMenu;

const { Clutter, GObject, GPaste, Pango, St } = imports.gi;

const ExtensionUtils = imports.misc.extensionUtils;
const Me = ExtensionUtils.getCurrentExtension();

const DeleteItemPart = Me.imports.deleteItemPart;

var GPasteItem = GObject.registerClass(
class GPasteItem extends PopupMenu.PopupMenuItem {
    _init(client, size, index) {
        super._init("");

        this._client = client;

        if (index <= 10) {
            this._indexLabel = new St.Label({
                text: index + ': '
            });
            this._indexLabelVisible = false;
        }

        this.connect('activate', this._onActivate.bind(this));
        this.connect('key-press-event', this._onKeyPressed.bind(this));

        this._deleteItem = new DeleteItemPart.GPasteDeleteItemPart(client, index);
        this.add(this._deleteItem.actor, { expand: true, x_align: St.Align.END });

        this.label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this.setTextSize(size);

        this.setIndex(index);
    }

    showIndex(state) {
        if (state) {
            if (!this._indexLabelVisible) {
                this.insert_child_at_index(this._indexLabel, 1);
            }
        } else if (this._indexLabelVisible) {
            this.remove_child(this._indexLabel);
        }
        this._indexLabelVisible = state;
    }

    refresh() {
        this.setIndex(this._index);
    }

    setIndex(index) {
        const oldIndex = this._index || -1;
        this._index = index;

        if (index == 0) {
            this.label.set_style("font-weight: bold;");
        } else if (oldIndex == 0) {
            this.label.set_style(null);
        }

        this._deleteItem.setIndex(index);

        if (index != -1) {
            this._client.get_element(index, (client, result) => {
                const text = client.get_element_finish(result).replace(/[\t\n\r]/g, ' ');
                if (text == this.label.get_text()) {
                    return;
                }
                this.label.clutter_text.set_text(text);
                if (oldIndex == -1) {
                    this.show();
                }
            });
        } else {
            this.label.clutter_text.set_text(null);
            this.hide();
        }
    }

    setTextSize(size) {
        this.label.clutter_text.max_length = size;
    }

    _onActivate(actor, event) {
        this._client.select(this._index, null);
    }

    _onKeyPressed(actor, event) {
        const symbol = event.get_key_symbol();
        if (symbol == Clutter.KEY_space || symbol == Clutter.KEY_Return) {
            this.activate(event);
            return Clutter.EVENT_STOP;
        }
        if (symbol == Clutter.KEY_BackSpace || symbol == Clutter.KEY_Delete) {
            this._client.delete(this._index, null);
            return Clutter.EVENT_STOP;
        }
        return Clutter.EVENT_PROPAGATE;
    }
});
