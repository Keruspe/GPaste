/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import GObject from 'gi://GObject?version=2.0';
import St from 'gi://St';

export const GPastePadding = GObject.registerClass(
class GPastePadding extends St.Label {
    _init() {
        super._init({ text: '', x_expand: true });
    }
});
