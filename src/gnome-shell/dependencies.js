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
Gio._promisify(GPaste.Client.prototype, 'list_histories', 'list_histories_finish');
Gio._promisify(GPaste.Client.prototype, 'search_text', 'search_text_finish');
Gio._promisify(GPaste.Client.prototype, 'get_item_at_index', 'get_item_at_index_finish');
Gio._promisify(GPaste.Client.prototype, 'get_item', 'get_item_finish');
// What an item's kind promises but its value does not carry: the image bytes,
// which would dwarf every listing if they rode along with it. A colour needs no
// call -- it is the item's value.
Gio._promisify(GPaste.Client.prototype, 'get_image', 'get_image_finish');

/**
 * The callback for a call nothing waits on: the menu has closed on it, or never
 * needed its answer, so a refusal -- a daemon busy handing its store over, say --
 * has nowhere to go but the log, and would otherwise be dropped without a trace.
 *
 * @param {string} finish - the name of the call's _finish method
 * @returns {Function} the callback to pass the call
 */
export function logFailure(finish) {
    return (client, result) => {
        try {
            client[finish](result);
        } catch (e) {
            console.error(e);
        }
    };
}

/**
 * What a row's own read failing is said with: at debug level, the menu
 * rebinding every row on each copy and each opening, so one message per binding
 * would fill the journal -- as the graphical tool's rows avoid too. A read
 * failing is a GLib.Error; anything else thrown on the way -- a programming
 * error filling the row -- is said as an error, which is what gets it noticed.
 *
 * @param {Error} error - what the read failed with
 */
export function debugFailure(error) {
    if (error instanceof GLib.Error)
        console.debug(`GPaste: ${error.message}`);
    else
        console.error(error);
}

/**
 * Whether a call failed because its daemon left -- the presence's to say next,
 * and nothing to log.
 *
 * Asked of the error's type first, as awaitReply () asks it:
 * GPaste.Client.is_daemon_gone_error () takes a GLib.Error, and GJS throws a
 * TypeError for anything else -- a programming error thrown down the call --
 * from inside the catch that asks, which then fails instead of sorting it.
 *
 * @param {Error} error - what the call failed with
 * @returns {boolean} whether the daemon was gone
 */
export function daemonGone(error) {
    return error instanceof GLib.Error && GPaste.Client.is_daemon_gone_error(error);
}

/**
 * Cancel the request still in flight and hand back the cancellable its successor
 * goes out on, so a caller keeps one field for "the request that is current".
 *
 * Every read the extension makes is one a newer binding, keystroke or reload may
 * replace, and cancelling is what lets go of the one replaced: the client fails
 * the call at once, so whatever awaits it resumes and releases what it holds.
 * The daemon is not told -- D-Bus has no way to call off a method already sent
 * -- so it still answers, and that reply is dropped on arrival.
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

/**
 * The width a row asks for without the @revealed buttons it shows only while
 * it is looked at -- under the pointer, or the keyboard on it -- for the row's
 * vfunc_get_preferred_width (): a menu is as wide as its widest row, so a
 * button appearing would otherwise widen the whole menu, where it is meant to
 * take its room from the row's expanding label. Measured while they show
 * rather than reserved while they do not, a button St has not styled yet
 * measuring nothing.
 *
 * @param {St.BoxLayout} row - the row, laid out by its box layout
 * @param {number[]} width - the minimum and natural widths its class asks for
 * @param {St.Widget[]} revealed - the buttons a look at the row shows
 * @param {number} forHeight - the height the width is asked for
 * @returns {number[]} the minimum and natural widths to ask for
 */
export function widthWithoutRevealed(row, [min, natural], revealed, forHeight) {
    const spacing = row.layout_manager.spacing;
    let shown = 0;

    for (const button of revealed) {
        if (button.visible)
            shown += button.get_preferred_width(forHeight)[1] + spacing;
    }

    return [min, Math.max(min, natural - shown)];
}
