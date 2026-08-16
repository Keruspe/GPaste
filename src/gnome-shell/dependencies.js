// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

import Gio from 'gi://Gio';
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
