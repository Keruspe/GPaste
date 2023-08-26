/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2023, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

import 'gi://Gio?version=2.0';
import 'gi://GLib?version=2.0';
import 'gi://GObject?version=2.0';
import 'gi://GPaste?version=2';
import 'gi://Pango?version=1.0';

import * as Config from 'resource:///org/gnome/shell/misc/config.js';

import gi from 'gi';

gi.require('Clutter', Config.LIBMUTTER_API_VERSION);
gi.require('St', Config.LIBMUTTER_API_VERSION);
