#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

"""Check that what gpaste-client says to the user goes through gettext.

xgettext only extracts what is marked, and a message left bare reads in English
under any locale beside its translated siblings, with no translator able to do
anything about it. So every error the client builds and every line it prints
whose text is a literal has to be marked -- a format that only places
translated words, as "%s\\n", carries no text of its own.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / 'src/client/gpaste-client.c'

# The message argument of g_set_error () and g_set_error_literal (), the fourth;
# of g_prefix_error () and fprintf (), the second; and the first of g_printerr
# (), g_print (), printf (), fputs () and puts (), when it is a bare literal.
ARG = r'(?:[^,()]|\([^()]*\))*,'
BARE = re.compile(
    r'\b(?:g_set_error(?:_literal)?\s*\(' + ARG * 3 +
    r'|(?:g_prefix_error|fprintf)\s*\(' + ARG +
    r'|(?:g_printerr|g_print|printf|fputs|puts)\s*\()\s*"(?P<text>(?:[^"\\]|\\.)*)"')
# What a format carries that is not text: its directives and escapes, and the
# options --help spells out, which are the command line's, not a language's.
NOT_TEXT = re.compile(r'%[-#0 +]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXcsp%]|\\x[0-9A-Fa-f]+|\\.|--?[A-Za-z][\w-]*')

# Left in English on purpose, each with the reason why.
ENGLISH = {
    # The error the daemon's GetItemAtIndex answers past the end, which the
    # client reports alike for several indexes.
    'No item at index %',
}


def main():
    source = CLIENT.read_text(encoding='utf-8')
    failures = [match['text'] for match in BARE.finditer(source)
                if re.search('[A-Za-z]', NOT_TEXT.sub('', match['text'])) and match['text'] not in ENGLISH]

    for text in failures:
        print(f'gpaste-client says "{text}" without marking it for translation', file=sys.stderr)

    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
