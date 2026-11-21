#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Synthetic shared-child/cyclic b-trees; no third-party inputs are read."""
import sqlite3
import struct


def generate(path, depth, table='config', cycle=False):
    path.unlink(missing_ok=True)
    db = sqlite3.connect(path)
    db.execute('PRAGMA page_size=4096')
    db.execute('CREATE TABLE config(name TEXT PRIMARY KEY NOT NULL,value BLOB) WITHOUT ROWID')
    # ls contains one layer with id 1 and name L.
    db.executemany('INSERT INTO config VALUES (?,?)', [('fmt_ver', b'2'), ('ls', b'\x0a\x05\x08\x01\x12\x01L')])
    db.execute('CREATE TABLE surface_1(tid INTEGER PRIMARY KEY NOT NULL,tile BLOB NOT NULL) WITHOUT ROWID')
    db.execute("INSERT INTO surface_1 VALUES (1, X'00')")
    root = db.execute('SELECT rootpage FROM sqlite_schema WHERE name=?', (table,)).fetchone()[0]
    db.commit()
    db.close()
    data = bytearray(path.read_bytes())
    page_size = 4096
    leaf = data[(root - 1) * page_size:root * page_size]
    assert leaf[0] == 10
    first_new = len(data) // page_size + 1
    for level in range(depth):
        child = root if cycle and level == depth - 1 else first_new + level
        # An index interior cell: child page, payload length, record bytes.
        cell = struct.pack('>I', child) + b'\x03\x02\x0f\x61'
        cells = (page_size - 12) // (len(cell) + 2) - 1
        page = bytearray(page_size)
        content = page_size - cells * len(cell)
        struct.pack_into('>BHHHBI', page, 0, 2, 0, cells, content, 0, child)
        for index in range(cells):
            offset = content + index * len(cell)
            struct.pack_into('>H', page, 12 + index * 2, offset)
            page[offset:offset + len(cell)] = cell
        if level == 0:
            data[(root - 1) * page_size:root * page_size] = page
        else:
            data.extend(page)
    data.extend(leaf)
    struct.pack_into('>I', data, 28, len(data) // page_size)
    data[92:96] = data[24:28]
    path.write_bytes(data)
