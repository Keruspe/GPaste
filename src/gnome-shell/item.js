// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import {PopupMenuItem} from 'resource:///org/gnome/shell/ui/popupMenu.js';

import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GObject from 'gi://GObject';
import GPaste from 'gi://GPaste?version=3';
import Pango from 'gi://Pango';
import St from 'gi://St';

import {GPasteDeleteButton} from './deleteButton.js';
import {GPasteFavouriteButton} from './favouriteButton.js';

// The grey the swatch is outlined in, so a colour close to the menu behind it is
// still a visible swatch rather than a hole. Spelled out, where the graphical
// tool's swatch gets the same line from Adwaita's .frame: that class is GTK's,
// the shell stylesheet defines nothing like it, and St's CSS has neither
// currentColor nor color-mix() to rebuild it with. Mid-grey rather than the
// theme's foreground because it has to hold up against both, and a swatch is
// drawn before the actor has a theme node to ask.
const SWATCH_BORDER = 'rgba(128, 128, 128, 0.5)';

// One stage listener for every row, and not one per row. Where the keyboard is
// has to be asked of the stage (see the constructor), but a focus move anywhere
// in the shell would otherwise run a handler on each of the several dozen rows
// the menu keeps alive, every one of them walking the focused actor's ancestors
// -- where only the row the focus left and the row it arrived on can have
// changed, and the focused actor's own ancestry names both.
//
// Tied to the rows' own lifetime, nothing here outliving every row and nothing
// else, and a listener left on the stage past the last row holding that row up
// with it. Rows count themselves in as they are built and out on 'destroy',
// which Clutter emits however the actor is torn down -- where destroy() below is
// a plain method, and any teardown that goes through the parent actor skips it.
const focusWatcher = {
    id: 0,
    rows: 0,
    row: null,

    watch(row) {
        if (this.rows++ === 0)
            this.id = global.stage.connect('notify::key-focus', () => this._moved());

        row.connect('destroy', () => this._forget(row));
    },

    _forget(row) {
        if (this.row === row)
            this.row = null;

        if (--this.rows === 0) {
            global.stage.disconnect(this.id);
            this.id = 0;
        }
    },

    _moved() {
        let row = global.stage.get_key_focus();

        while (row !== null && !(row instanceof GPasteItem))
            row = row.get_parent();

        // A move within one row -- its label to one of its buttons -- leaves
        // both answers where they were.
        if (row === this.row)
            return;

        const left = this.row;

        this.row = row;
        left?._updateActionsVisibility();
        row?._updateActionsVisibility();
    },
};

export class GPasteItem extends PopupMenuItem {
    // Registered here rather than through GObject.registerClass (class ...)
    // because a binding pool is per-class and only exists once the class does.
    // StWidget then puts a key controller on every instance whose type chain
    // has one, and that controller walks the chain from this class up, so these
    // are consulted before the space/Return PopupBaseMenuItem installs.
    static {
        GObject.registerClass(this);

        const bindingPool = this.get_binding_pool();
        const deleteItem = obj => {
            // Nothing to delete until the row's fetch has landed (see activate).
            if (obj._uuid)
                obj._client.delete_item(obj._uuid, null, null);

            return Clutter.EVENT_STOP;
        };

        bindingPool.install_closure('delete', Clutter.KEY_BackSpace, 0, deleteItem);
        bindingPool.install_closure('delete', Clutter.KEY_Delete, 0, deleteItem);
    }

    constructor(client, size, slotIndex, index, uuid = null) {
        // hover: false keeps the pointer from stealing key focus from the search
        // entry (Fix #435) without dropping can_focus, so the rows stay reachable
        // with the arrow keys.
        super('', {hover: false});
        this.label.set_x_expand(true);

        this._client = client;
        this._index = -1;
        this._uuid = null;
        this._generation = 0;
        // The image preview has a generation of its own: a settings change asks
        // for a new one without the row being refilled, so the row's would not
        // move and both fetches would think themselves current.
        this._previewGeneration = 0;
        // The full (untruncated) text currently set on the label. Compared
        // against in _setValue to skip redundant set_text() calls; we can't use
        // label.get_text() for that because max_length truncates what the label
        // stores, so a long value would never compare equal.
        this._displayedText = null;

        if (slotIndex <= 9) {
            this._indexLabel = new St.Label({
                text: `${slotIndex}: `,
            });
            this._indexLabelVisible = false;
        }

        // What the row shows of itself beyond its text: a thumbnail for an image,
        // a swatch for a colour, nothing for anything else. One slot, since an
        // item is never both, and an empty St.Bin takes no room.
        //
        // Preview first, then the text: the label is the only child that
        // expands, so it is the only one whose width the pin and delete buttons
        // take when they appear, and everything before it stays where it is.
        // It is also where a list row puts an image — a thumbnail leads the row,
        // the text follows it, and the row's own actions sit at the end — which
        // is the layout the graphical tool's rows already have.
        this._previewBin = new St.Bin({y_align: Clutter.ActorAlign.CENTER});
        this.insert_child_below(this._previewBin, this.label);

        // What the row last displayed, so a settings change knows whether it has
        // anything to redo.
        this._kind = null;
        this._imagesPreview = false;
        this._imagesPreviewSize = 0;
        this._favourited = false;

        // The bin before the star, as in the graphical tool: a pinned row shows
        // its star whether the pointer is on it or not, and only the last child
        // of the row keeps its place when the one before it appears. With the
        // star ahead of the bin, the badge would jump a button's width left the
        // moment the pointer arrives.
        this._deleteItem = new GPasteDeleteButton(client, this._uuid);
        this.add_child(this._deleteItem);

        this._favouriteItem = new GPasteFavouriteButton(client, this._uuid);
        this.add_child(this._favouriteItem);

        // Both actions wait to be asked for: they show on the row the pointer is
        // on, and on the row the keyboard is on — `active` being what the shell
        // calls the latter, since `hover: false` above leaves the pointer out of
        // it. The star is the exception, again as in the graphical tool: on a
        // pinned item it is not a button but a badge, and a badge that only
        // appears under the pointer says nothing.
        //
        // Hidden outright rather than faded out: a line of text is taller than a
        // symbolic icon, so the row's height is the label's either way, and
        // hiding them gives their width back to it.
        //
        // The buttons themselves are the third thing that counts as asked for:
        // they are built with can_focus set, and tabbing into one is the
        // keyboard still on this row — but not to PopupBaseMenuItem, whose
        // key-focus-out clears `active` with no exemption for its own children.
        // Without something saying so the button would be hidden by the very
        // keypress that reached it, and an unmapped actor is one Clutter drops
        // key focus for, stranding the keyboard on nothing.
        //
        // Where the keyboard is is therefore asked of the stage, and answered
        // once the move has settled: key-focus-out is emitted on the actor
        // losing focus before the stage has recorded the actor gaining it, so
        // neither the row's nor a button's own focus signals can tell "leaving
        // this row" from "arriving at one of its buttons" while they run.
        // notify::key-focus is emitted after the new focus is in place, which is
        // the only moment the question has an answer. Asked once for all the
        // rows there are, through focusWatcher above.
        focusWatcher.watch(this);

        this.connect('notify::hover', () => this._updateActionsVisibility());
        this.connect('notify::active', () => this._updateActionsVisibility());
        this._updateActionsVisibility();

        this.label.clutter_text.ellipsize = Pango.EllipsizeMode.END;
        this.setTextSize(size);

        // Search rows are addressed by uuid (the search returns uuids); history
        // rows by their index.
        if (uuid !== null)
            this.setUuid(uuid).catch(console.error);
        else
            this.setIndex(index).catch(console.error);
    }

    destroy() {
        // Discard any in-flight setIndex()/setUuid() fetch, and any preview
        // fetch with it: bumping the generations makes their post-await guards
        // bail out instead of touching this now-finalized actor.
        this._generation++;
        this._previewGeneration++;
        super.destroy();
    }

    showIndex(state) {
        // Only the first ten rows get a ctrl-index label (there is no ctrl+10),
        // so the rest have nothing to show either way.
        if (!this._indexLabel)
            return;

        if (state) {
            // Anchored to the preview bin rather than to a fixed slot: the
            // index leads the row and the bin sits between it and the label. A
            // hardcoded position says none of that, and is what any change to
            // the row's children silently breaks.
            if (!this._indexLabelVisible)
                this.insert_child_below(this._indexLabel, this._previewBin);
        } else if (this._indexLabelVisible) {
            this.remove_child(this._indexLabel);
        }
        this._indexLabelVisible = state;
    }

    get uuid() {
        return this._uuid;
    }

    refresh() {
        // A row is addressed the way it was filled: -2 marks one that came from
        // a uuid (a search, the favourites), where its index means nothing.
        if (this._index === -2)
            this.setUuid(this._uuid).catch(console.error);
        else
            this.setIndex(this._index).catch(console.error);
    }

    async setIndex(index) {
        const generation = ++this._generation;
        this._index = index;

        if (index === -1) {
            this._setValue(null);
        } else {
            // A recycled row goes on showing the item it held until the fetch
            // lands, but it is no longer that item, so it stops answering for
            // it now rather than when the reply comes: activating the row — or
            // clicking its star or its bin — in that window would otherwise act
            // on the item the row used to show, the star flipping the pin the
            // wrong way, since the flag it reads is that item's too. Disarmed,
            // not aimed elsewhere.
            this._uuid = null;
            this._disarmActions();

            const item = await this._client.get_item_at_index(index, null);
            if (generation !== this._generation)
                return;
            this._uuid = item.get_uuid();
            this._setValue(item.get_value(), item.is_favourite(), item.get_kind());
        }
    }

    async setUuid(uuid) {
        const generation = ++this._generation;
        this._index = -2;
        this._uuid = uuid;
        // The row's own uuid is known here, but the star reads the pin flag on
        // top of it and that only arrives with the item (see setIndex).
        this._disarmActions();

        if (uuid == null) {
            this._setValue(null);
        } else {
            const item = await this._client.get_item(uuid, null);
            if (generation !== this._generation)
                return;
            this._setValue(item.get_value(), item.is_favourite(), item.get_kind());
        }
    }

    // Both actions are addressed by uuid, and a button with none is inert. The
    // star is more than inert: it is a badge on a pinned row, so a row that has
    // stopped answering for its old item must stop showing that item's pin
    // state too, or it claims one that belongs to something else.
    _disarmActions() {
        this._favouriteItem.setUuid(null);
        this._deleteItem.setUuid(null);
        this._favourited = false;
        this._updateActionsVisibility();
    }

    _setValue(value, favourite = false, kind = null) {
        this.label.set_style(this._index === 0 ? 'font-weight: bold;' : null);

        if (this._index === -1) {
            this._uuid = null;
            this._displayedText = value || '';
            this.label.clutter_text.set_text(this._displayedText);
            this.hide();
        } else {
            // The decoration a kind calls for, composed by libgpaste rather
            // than spelled a second time here: it translates the bare words
            // through the GPaste domain -- the very domain this extension
            // declares -- and it runs in this process, so they come out in the
            // shell's own locale rather than the daemon's. A row still waiting
            // for its item has neither a value nor a kind to hand it, and
            // ItemKind has no introspected member for "none" to pass in place
            // of the one it lacks.
            const text = value == null
                ? ''
                : GPaste.util_one_line(GPaste.util_display_string(value, kind));
            if (text !== this._displayedText) {
                this._displayedText = text;
                this.label.clutter_text.set_text(text);
            }

            this.show();
        }

        // The undecorated value: what the swatch needs, and what the label above
        // is drawn *from* rather than what it shows.
        this._value = value;
        this._kind = kind;
        this._updatePreview();

        this._favouriteItem.setUuid(this._uuid);
        this._favouriteItem.setFavourite(favourite);
        this._deleteItem.setUuid(this._uuid);

        this._favourited = favourite;
        this._updateActionsVisibility();
    }

    _updateActionsVisibility() {
        const focus = global.stage.get_key_focus();
        const revealed = this.hover || this.active || (focus !== null && this.contains(focus));

        this._deleteItem.visible = revealed;
        this._favouriteItem.visible = revealed || this._favourited;
    }

    setTextSize(size) {
        this.label.clutter_text.max_length = size;
    }

    // The two image-preview settings, pushed by the indicator rather than
    // watched here: one subscription for the whole menu instead of one per row.
    // Only images are affected — a colour swatch is neither an image nor sized
    // like one.
    setImagesPreview(enabled, size) {
        if (enabled === this._imagesPreview && size === this._imagesPreviewSize)
            return;

        this._imagesPreview = enabled;
        this._imagesPreviewSize = size;

        if (this._kind === GPaste.ItemKind.IMAGE)
            this._updatePreview();
    }

    // Drop whatever the row was showing and show what it should show now. Only
    // the image costs a call -- its bytes would dwarf every listing if they rode
    // along with the item -- and that one is guarded like the row's own fetch,
    // by a generation of its own: a recycled row must not be painted with the
    // item it used to hold, nor a resized one with the size it asked for before.
    // A colour needs no call at all: it is the item's value.
    _updatePreview() {
        // Whatever was being fetched was for the row as it stood a moment ago:
        // another item, or the same image at another size. Bumped here rather
        // than in _showImage so that a row that has just stopped being an image
        // discards the fetch too, having no _showImage of its own to bump it.
        const generation = ++this._previewGeneration;

        this._previewBin.child = null;

        if (!this._uuid)
            return;

        if (this._kind === GPaste.ItemKind.IMAGE && this._imagesPreview)
            this._showImage(generation).catch(console.error);
        else if (this._kind === GPaste.ItemKind.COLOR)
            this._showColor();
    }

    async _showImage(generation) {
        const size = Math.max(this._imagesPreviewSize, 10);
        const bytes = await this._client.get_image(this._uuid, null);

        if (generation !== this._previewGeneration)
            return;

        // St loads a GLoadableIcon through GdkPixbuf, scaled into the requested
        // size with its aspect ratio kept, so the bytes need no decoding here.
        // Uncached, since a GBytesIcon has no string form to key a cache on:
        // one decode each time a row is filled, which is what a few image rows
        // cost and the reason this stays a small thumbnail.
        this._previewBin.child = new St.Icon({
            gicon: Gio.BytesIcon.new(bytes),
            icon_size: size,
            style: 'margin-right: 6px;',
        });
    }

    _getSwatchSize() {
        // Measure text, not the menu row: this preview takes part in the row's
        // allocation, so measuring the row would make the result feed back into
        // its own size. The padding gives the framed colour the weight of an
        // icon, while the bounds keep it a compact list decoration.
        const [, natural] = this.label.get_preferred_height(-1);

        return Math.min(32, Math.max(16, Math.ceil((natural + 4) / 4) * 4));
    }

    _showColor() {
        const color = this._value;

        // The item's value is its colour: no round trip to make. It reached a
        // colour item only because gdk_rgba_parse() accepted it, so it is a
        // colour and not arbitrary text; this checks the shape all the same,
        // because what it goes into is a stylesheet and a value off the
        // clipboard has no business ending a declaration early.
        if (!color || !/^[a-zA-Z0-9#(),.%\s]+$/.test(color))
            return;

        const size = this._getSwatchSize();

        this._previewBin.child = new St.Widget({
            style: `background-color: ${color}; border: 1px solid ${SWATCH_BORDER}; border-radius: 4px; margin-right: 6px;`,
            width: size,
            height: size,
        });
    }

    activate(event) {
        // A row is materialised before its content is fetched, so until that
        // async round-trip lands it has no uuid to select — and GJS would throw
        // on the null rather than let the call through. Stay a no-op (leaving
        // the menu open to try again) instead of closing it over nothing.
        if (!this._uuid)
            return;

        this._client.select(this._uuid, null, null);
        super.activate(event);
    }
}
