// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import St from 'gi://St';

/**
 * A labelled button of an inline action row, appended to @box. A history row
 * and an item row each open one such row under them, and the two are meant to
 * read as one idiom: this is what keeps their buttons alike.
 *
 * @param {St.BoxLayout} box - the row the button goes in
 * @param {string} label - what the button says
 * @param {Function} action - what a click does
 * @returns {St.Button} the button
 */
export function addActionButton(box, label, action) {
    const button = new St.Button({
        style_class: 'button',
        label,
        x_expand: true,
        can_focus: true,
    });

    button.connect('clicked', action);
    box.add_child(button);

    return button;
}
