// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GPaste from 'gi://GPaste?version=3';

import * as Config from 'resource:///org/gnome/shell/misc/config.js';

import gi from 'gi';

gi.require('Clutter', Config.LIBMUTTER_API_VERSION);
gi.require('St', Config.LIBMUTTER_API_VERSION);

// GPaste.Client.new is handled manually in indicator.js: Gio._promisify cannot
// replace a static constructor on the class object inside gnome-shell.
Gio._promisify(GPaste.Client.prototype, 'get_favourites', 'get_favourites_finish');
Gio._promisify(GPaste.Client.prototype, 'get_history_size', 'get_history_size_finish');
Gio._promisify(GPaste.Client.prototype, 'search', 'search_finish');
Gio._promisify(GPaste.Client.prototype, 'get_item_at_index', 'get_item_at_index_finish');
Gio._promisify(GPaste.Client.prototype, 'get_item', 'get_item_finish');
// What an item's kind promises but its value does not carry: the image bytes,
// which would dwarf every listing if they rode along with it. A colour needs no
// call -- it is the item's value.
Gio._promisify(GPaste.Client.prototype, 'get_image', 'get_image_finish');

/**
 * Cancel the request still in flight and hand back the cancellable its successor
 * goes out on, so a caller keeps one field for "the request that is current".
 *
 * Every read the extension makes is one a newer binding, keystroke or reload may
 * replace, and cancelling is what makes replacing cheap: the client fails the
 * call, so the daemon stops carrying an image's bytes over the bus, or matching a
 * search term, for a reply that was going to be dropped on arrival.
 *
 * @param {?Gio.Cancellable} cancellable - the one being replaced, if any
 * @returns {Gio.Cancellable} the one to issue the next request on
 */
export function replaceCancellable(cancellable) {
    cancellable?.cancel();

    return new Gio.Cancellable();
}

/**
 * Await the reply to a request issued on @cancellable, and say whether it is
 * still wanted.
 *
 * Both halves are needed. Cancelling fails the call, which arrives as a
 * rejection of G_IO_ERROR_CANCELLED; but it does not unqueue a reply already on
 * its way, so the request a newer one replaced can still land, and land
 * successfully. Hence the question is put to @cancellable -- the caller's own
 * local, the one the request went out on, rather than whichever its owner holds
 * by the time this returns.
 *
 * A real failure is the caller's to report, and is rethrown.
 *
 * @param {Gio.Cancellable} cancellable - the one the request went out on
 * @param {Promise} call - the request
 * @returns {Array} whether the reply is still wanted, and what it carried
 */
export async function awaitReply(cancellable, call) {
    let value;

    try {
        value = await call;
    } catch (e) {
        // Asked of the error's type first: matches () is GLib.Error's, and a
        // rejection that is not one -- a programming error thrown anywhere down
        // the call -- would leave that call throwing a TypeError about matches
        // itself, which is then the only failure the caller ever sees.
        if (!(e instanceof GLib.Error) || !e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.CANCELLED))
            throw e;

        return [false, null];
    }

    return [!cancellable.is_cancelled(), value];
}
