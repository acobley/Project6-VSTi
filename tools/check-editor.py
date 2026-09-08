#!/usr/bin/env python3
"""Guard one editor invariant that no runtime test can reach.

`CControl::setValue` assigns the value and nothing else - it does NOT mark
the view dirty. A control whose displayed value is set without an
accompanying `invalid()` therefore repaints only when something else happens
to dirty the same region, which reads as "it works sometimes".

That is not hypothetical: VocalFilter shipped its Voice switch that way. It was correct
under the mouse, because SpySlider's own handler invalidates, and
intermittent under host automation, because the update path did not.

So: `setValueNormalized` may appear in Project6Editor.cpp exactly once,
inside `showValue`, which pairs it with `invalid()`.

    python3 tools/check-editor.py        # exits 1 and names the line on failure

WHAT THIS DOES NOT DO: it cannot tell you that showValue is CALLED where it
should be, only that nothing bypasses it. It is a guard against reintroducing
a known mistake, not a proof of correctness.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EDITOR = os.path.join(HERE, '..', 'source', 'Project6Editor.cpp')


def strip_comments(text):
    """Blank out // and /* */ so a mention in prose is not a call."""
    text = re.sub(r'/\*.*?\*/', lambda m: ' ' * len(m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)


def function_span(text, name):
    """Character range of a member function's body, crudely but adequately."""
    m = re.search(r'\n[\w:&<>\*\s]*Project6Editor::' + name + r'\s*\(', text)
    if not m:
        return None
    i = text.index('{', m.end())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return (i, j)
    return None


def main():
    raw = open(EDITOR, encoding='utf-8').read()
    code = strip_comments(raw)

    span = function_span(code, 'showValue')
    if span is None:
        print('check-editor: showValue is gone from Project6Editor.cpp')
        return 1

    bad = []
    for m in re.finditer(r'setValueNormalized\s*\(', code):
        if not (span[0] <= m.start() <= span[1]):
            bad.append(raw.count('\n', 0, m.start()) + 1)

    if bad:
        print('check-editor: setValueNormalized called outside showValue, '
              'so a control can be updated without being redrawn')
        for line in bad:
            print(f'    source/Project6Editor.cpp:{line}')
        print('  Use showValue(control, normalized) - it pairs the set with '
              'invalid().')
        return 1

    # And showValue must actually still do the invalidate.
    body = code[span[0]:span[1]]
    if 'invalid' not in body:
        print('check-editor: showValue no longer calls invalid(), which is '
              'the only reason it exists')
        return 1

    print('check-editor: ok - setValueNormalized appears only inside '
          'showValue, which invalidates')
    return 0


if __name__ == '__main__':
    sys.exit(main())
