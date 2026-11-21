#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Generate a valid ~252 MiB file at test time; never add it to fuzz seeds."""
import pathlib
import sqlite3
import subprocess
import sys

exe, filename = sys.argv[1:]
path = pathlib.Path(filename)
try:
    path.unlink(missing_ok=True)
    db = sqlite3.connect(path)
    db.execute('PRAGMA page_size=4096')
    db.execute('CREATE TABLE config(name TEXT PRIMARY KEY,value BLOB) WITHOUT ROWID')
    db.executemany('INSERT INTO config VALUES (?,?)', [('fmt_ver', b'2'), ('ls', b'\x0a\x05\x08\x01\x12\x01L')])
    db.execute('CREATE TABLE surface_1(tid INTEGER PRIMARY KEY NOT NULL,tile BLOB NOT NULL) WITHOUT ROWID')
    for tid in range(63):
        db.execute('INSERT INTO surface_1 VALUES (?,zeroblob(4194304))', (tid,))
    db.commit()
    db.close()
    size = path.stat().st_size
    assert 250 * 1024**2 < size <= 256 * 1024**2, size
    subprocess.run([exe, '--large', str(path)], check=True, timeout=40)
    print(f'default budgets accepted {size} file bytes and {63 * 4194304} tile bytes')
finally:
    path.unlink(missing_ok=True)
