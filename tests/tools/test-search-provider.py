#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# The overview's search provider asks a daemon that is running, and starts
# none: GNOME Shell activates the bus name of one registered without
# AutoStart=false at the first search, which would have the standalone daemon
# record every copy from then on behind the user's back -- and raise its storage
# questions from an overview keystroke.

import configparser
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
REGISTRATION = ROOT / 'data/search-provider/org.gnome.GPaste.search-provider.ini'

provider = configparser.ConfigParser(interpolation=None)
provider.optionxform = str
provider.read(REGISTRATION, encoding='utf-8')
section = provider['Shell Search Provider']

if section.get('AutoStart') != 'false':
    print(f'{REGISTRATION.name} has the overview start a daemon: AutoStart is {section.get("AutoStart")!r}')
    sys.exit(1)
