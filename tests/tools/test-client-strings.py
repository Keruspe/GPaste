#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

# What gpaste-client says to the user, read off its source: every error it
# builds is marked for translation, a message left bare reading in English
# beside its translated siblings; and no translation is handed to a printf-like
# call as its format, where a % a translator wrote would read as a directive.

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'src' / 'client' / 'gpaste-client.c'

ARG = r'(?:[^,()]|\([^()]*\))*,'
# The message of g_set_error () and g_set_error_literal (), their fourth
# argument, when it is a bare literal.
BARE_ERROR = re.compile(r'\bg_set_error(?:_literal)?\s*\(' + ARG * 3 + r'\s*"(?P<text>(?:[^"\\]|\\.)*)"')
# A printf-like call, or a GLib log one, whose format is a translation.
TRANSLATED_FORMAT = re.compile(r'\b(?P<call>printf|g_print|g_printerr|g_critical|g_warning|g_message|g_info|g_debug|g_error)'
                               r'\s*\(\s*_\s*\(|\bfprintf\s*\(' + ARG + r'\s*_\s*\(')


def main():
    source = CLIENT.read_text(encoding='utf-8')
    failures = [f'builds the error "{m["text"]}" without marking it for translation' for m in BARE_ERROR.finditer(source)]
    failures += [f'hands a translation to {m["call"] or "fprintf"} as its format' for m in TRANSLATED_FORMAT.finditer(source)]

    for failure in failures:
        print(f'FAIL: gpaste-client {failure}', file=sys.stderr)

    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
