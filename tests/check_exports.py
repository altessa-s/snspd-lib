# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
import pathlib
import re
import subprocess
import sys

library, headers, kind = sys.argv[1:]
flags = ['-gU'] if sys.platform == 'darwin' else (['-D', '--defined-only'] if kind == 'shared' else ['-g', '--defined-only'])
lines = subprocess.check_output(['nm', *flags, library], text=True).splitlines()
actual = set()
for line in lines:
    parts = line.split()
    if len(parts) < 3 or parts[-2].upper() == 'U':
        continue
    symbol = parts[-1].removeprefix('_') if sys.platform == 'darwin' else parts[-1]
    # Cross-object helpers in static archives have a reserved private prefix.
    if kind == 'static' and symbol.startswith('spd__internal_'):
        continue
    # Clang's common ASan registration flag is compiler instrumentation.
    if kind == 'static' and symbol == '___asan_globals_registered' and parts[-2] == 'C':
        continue
    actual.add(symbol)
public = '\n'.join(p.read_text() for p in pathlib.Path(headers).glob('*.h'))
expected = set(re.findall(r'SPD_API[^;]*?\b(spd_\w+)\s*\(', public, re.S))
assert len(expected) == 9, expected
assert actual == expected, ('missing', expected - actual, 'extra', actual - expected)
print('exact public export set: 9 functions')
