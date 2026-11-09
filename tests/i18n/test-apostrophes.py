#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
# SPDX-License-Identifier: BSD-2-Clause

"""Check that no string a translator is handed spells an apostrophe in ASCII.

An apostrophe the user reads is the typographic one, as the HIG asks (see
po/AGENTS.md): the sources hand translators what they then copy, so an ASCII
one there comes back in every catalog. Checked across what po/POTFILES.in
lists -- the marked strings of the C and JavaScript sources, and the
translatable text of the data files -- for an ASCII apostrophe between two
letters, which is where a word's own goes.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASCII = re.compile(r"[A-Za-z]'[A-Za-z]")

# A marker's call, every literal among its arguments read: the context and
# the message of a pgettext (), the singular and the plural of an ngettext ().
C_LITERAL = r'"(?:[^"\\]|\\.)*"'
C_MARKER = re.compile(r'(?:^|[^A-Za-z0-9_])(?:_|N_|C_|NC_|Q_|ngettext|g_dngettext|g_dpgettext2)\s*\(')
JS_LITERAL = r"'(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\"|`[^`]*`"
# What xgettext extracts from JavaScript: its own keywords for the language
# and the glib preset's (po/meson.build), whichever of them the extension
# imports from GNOME Shell's extension.js or binds itself.
JS_MARKER = re.compile(r'(?:^|[^A-Za-z0-9_.])(?:_|N_|C_|NC_|gettext|dgettext|dcgettext|ngettext|dngettext|pgettext|dpgettext)\(')
XML_TEXT = re.compile(r'<(summary|description|name|p|li|caption)\b[^>]*>(.*?)</\1>', re.S)
DESKTOP_VALUE = re.compile(r'^(?:Name|GenericName|Comment|Keywords|X-GNOME-FullName)=(.*)$', re.M)


def arguments(body, start, literal):
    """The text of the arguments of the call whose parenthesis opens at
    @start, up to the one closing it, the parentheses in @literal's strings
    aside."""
    depth = 0
    for token in re.finditer(literal + r'|[()]', body[start:]):
        if token[0] == '(':
            depth += 1
        elif token[0] == ')':
            depth -= 1
            if depth == 0:
                return body[start:start + token.end()]
    return body[start:]


def marked(entry, body):
    """Whether @body holds anything this reads as marked: a marker's call in a
    source, whatever its arguments -- `_(shortcut->portal_label)` marks no
    literal of its own -- a translatable text in a data file."""
    if entry.endswith(('.c', '.h')):
        return C_MARKER.search(body) is not None
    if entry.endswith('.js'):
        return JS_MARKER.search(body) is not None
    return any(True for _ in strings(entry, body))


def strings(entry, body):
    if entry.endswith(('.c', '.h')):
        for match in C_MARKER.finditer(body):
            yield from re.findall(C_LITERAL, arguments(body, match.end() - 1, C_LITERAL))
    elif entry.endswith('.js'):
        for match in JS_MARKER.finditer(body):
            for text in re.findall(JS_LITERAL, arguments(body, match.end() - 1, JS_LITERAL)):
                yield text[1:-1].replace("\\'", "'")
    elif '.desktop' in entry:
        yield from DESKTOP_VALUE.findall(body)
    else:
        yield from (text for _, text in XML_TEXT.findall(body))


# What the reader has to see, checked before the tree is: a string it reads
# past would let the tree pass silently.
SAMPLES = (
    ('a.js', """pgettext('verb', "Don't Empty")"""),
    ('a.js', """ngettext('%d item', "%d item's", n)"""),
    ('a.js', """C_('verb', "Don't Empty")"""),
    ('a.js', """N_("Don't Empty")"""),
    ('a.js', """gettext("Don't Empty")"""),
    ('a.js', """dpgettext('GPaste', 'verb', "Don't Empty")"""),
    ('b.c', """C_ ("verb", "Don't Empty")"""),
    ('b.c', """ngettext ("%u item", "%u item's", n)"""),
    ('b.c', """g_dngettext (NULL, "%u item", "%u item's", n)"""),
)


def main():
    for entry, body in SAMPLES:
        if not any(ASCII.search(text) for text in strings(entry, body)):
            sys.exit(f'the apostrophe check reads past the one in {body}')
    if marked('c.c', 'puts ("Don\'t");'):
        sys.exit('the apostrophe check reads a call no marker makes as marked')

    listed = [line.strip() for line in (ROOT / 'po/POTFILES.in').read_text().splitlines()
              if line.strip() and not line.startswith('#')]
    bodies = {entry: (ROOT / entry).read_text(encoding='utf-8') for entry in listed}
    # A file listed for its strings in which nothing reads as marked is one
    # marked some way this does not know, which would otherwise go unchecked
    # rather than fail.
    failures = [f'{entry}: nothing read as marked, though it is listed for its strings'
                for entry, body in bodies.items() if not marked(entry, body)]
    failures += [f'{entry}: {text.strip()[:80]}' for entry, body in bodies.items() for text in strings(entry, body)
                 if ASCII.search(text)]

    for failure in failures:
        print(f'an ASCII apostrophe in {failure}', file=sys.stderr)

    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
