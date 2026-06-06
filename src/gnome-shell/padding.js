// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import GObject from 'gi://GObject';
import St from 'gi://St';

export const GPastePadding = GObject.registerClass(
class GPastePadding extends St.Label {
    _init() {
        super._init({text: '', x_expand: true});
    }
});
