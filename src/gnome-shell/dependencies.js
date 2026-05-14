/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import Gio from 'gi://Gio?version=2.0';
import GPaste from 'gi://GPaste?version=2';

import * as Config from 'resource:///org/gnome/shell/misc/config.js';

import gi from 'gi';

gi.require('Clutter', Config.LIBMUTTER_API_VERSION);
gi.require('St', Config.LIBMUTTER_API_VERSION);

Gio._promisify(GPaste.Client, 'new', 'new_finish');
Gio._promisify(GPaste.Client.prototype, 'get_history_name', 'get_history_name_finish');
Gio._promisify(GPaste.Client.prototype, 'get_history_size', 'get_history_size_finish');
Gio._promisify(GPaste.Client.prototype, 'search', 'search_finish');
Gio._promisify(GPaste.Client.prototype, 'get_element_at_index', 'get_element_at_index_finish');
Gio._promisify(GPaste.Client.prototype, 'get_element', 'get_element_finish');
