# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
from contextlib import closing
import json
import pathlib
import sqlite3
import subprocess
import sys
import tempfile

from generate_fixtures import layer

tool, directory = sys.argv[1:]
fixture = pathlib.Path(directory) / 'valid.spd'
for args in [[], ['--json']]:
    a = subprocess.check_output([tool, *args, str(fixture)])
    b = subprocess.check_output([tool, *args, str(fixture)])
    assert a == b
    assert b'PNG' not in a and b'ffd8' not in a
report = json.loads(subprocess.check_output([tool, '--json', str(fixture)]))
assert report['layer_count'] == 2 and report['extra_table_count'] == 1
assert report['layers'][0]['grid'] == {'min_tx': 1952, 'max_tx': 1955, 'min_ty': 1953, 'max_ty': 1957}
text = subprocess.check_output([tool, str(fixture)]).decode().replace('\r\n', '\n')
assert 'grid_tx 1952 1955\ngrid_ty 1953 1957\n' in text
assert 'bounds_pixels -128 0 512 640\n' in text
huge = fixture.with_name('huge-tid.spd')
huge_report = json.loads(subprocess.check_output([tool, '--json', str(huge)]))
assert huge_report['layers'][0]['grid'] == {
    'min_tx': 2251799813685247, 'max_tx': 2251799813685247,
    'min_ty': 4095, 'max_ty': 4095}
huge_text = subprocess.check_output([tool, str(huge)]).decode().replace('\r\n', '\n')
assert 'grid_tx 2251799813685247 2251799813685247\ngrid_ty 4095 4095\n' in huge_text
assert report['layers'][1]['grid'] is None
assert report['layers'][1]['field3'] == 2 and report['layers'][1]['field4'] == 1
with tempfile.TemporaryDirectory() as directory:
    target = pathlib.Path(directory) / 'thumbnail.jpg'
    subprocess.check_output([tool, '--thumbnail', str(target), str(fixture)])
    assert target.read_bytes() == b'\xff\xd8\xff\xd9'
    assert subprocess.run([tool, '--thumbnail', str(target), str(fixture)], capture_output=True).returncode == 1
    assert target.read_bytes() == b'\xff\xd8\xff\xd9'
    before = fixture.read_bytes()
    assert subprocess.run([tool, '--thumbnail', str(fixture), str(fixture)], capture_output=True).returncode == 1
    assert fixture.read_bytes() == before
assert subprocess.run([tool, '--strict', str(fixture.with_name('unknown-version.spd'))], capture_output=True).returncode == 1
assert subprocess.run([tool, '--require-png-128', str(fixture.with_name('non-png.spd'))], capture_output=True).returncode == 1
assert subprocess.run([tool, '--bad-option'], capture_output=True).returncode == 2
print('deterministic text/JSON and thumbnail output: OK')

utf8_fixture = fixture.with_name('utf8.spd')
u = json.loads(subprocess.check_output([tool, '--json', str(utf8_fixture)]))
assert u['layers'][0]['name'] == 'Слой é 日本 😀'
assert next(e for e in u['config'] if e['key'] == 'text.key')['value'] == 'Слой é 日本 😀'
assert 'Слой é 日本 😀' in subprocess.check_output([tool, str(utf8_fixture)]).decode('utf-8')
bad_fixture = fixture.with_name('invalid-utf8.spd')
b = json.loads(subprocess.check_output([tool, '--json', str(bad_fixture)]))
assert b['layers'][0]['name'] == {'hex': 'ffc080eda080f4908080e282'}
assert any(e['key'] == {'hex': 'ff'} for e in b['config'])
assert 'value' not in next(e for e in b['config'] if e['key'] == 'text.key')
assert '{"hex":"ffc080eda080f4908080e282"}' in subprocess.check_output([tool, str(bad_fixture)]).decode('utf-8')

# Exercise each rejected encoding separately, including every boundary class.
with tempfile.TemporaryDirectory() as directory:
    target = pathlib.Path(directory) / 'encoding.spd'
    target.write_bytes(fixture.read_bytes())
    for raw in [b'\x80', b'\xc0\x80', b'\xe0\x80\x80', b'\xed\xa0\x80',
                b'\xf0\x80\x80\x80', b'\xf4\x90\x80\x80', b'\xf5\x80\x80\x80',
                b'\xe2\x82', b'\xc2x', b'\xe2\x82x',
                '\u0080\u07ff\u0800\ud7ff\ue000\uffff\U00010000\U0010ffff'.encode(),
                b'quote" slash\\ nul\0 newline\n']:
        # The connection context commits/rolls back but does not close the file.
        with closing(sqlite3.connect(target)) as con:
            with con:
                con.execute("UPDATE config SET value=? WHERE name='ls'", (layer(1, raw),))
        result = json.loads(subprocess.check_output([tool, '--json', str(target)]))
        try:
            expected = raw.decode('utf-8')
        except UnicodeDecodeError:
            expected = {'hex': raw.hex()}
        assert result['layers'][0]['name'] == expected
