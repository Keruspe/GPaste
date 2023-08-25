/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPastePadding = GObject.registerClass(
class GPastePadding extends St.Label {
    _init() {
        super._init({ text: '', x_expand: true });
    }
});
