#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Generate only synthetic SQLite/PNG fixtures. No external input is read."""
import pathlib
import sqlite3
import struct
import sys
import zlib

from generate_btrees import generate as generate_btree


def vi(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    return bytes(out + bytes([n]))


def field(n, value):
    return vi(n * 8 + 2) + vi(len(value)) + value


def layer(n, name, suffix=b''):
    return field(1, b'\x08' + vi(n) + field(2, name) + suffix)


def png(width=128, height=128):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = (b'\0' + b'\x20\x40\x60\xff' * width) * height
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def tile_id(tx, ty):
    """Column-major grid; synthetic coordinates intentionally differ by axis."""
    return tx * 4096 + ty


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    names = ['valid', 'unknown-version', 'missing-table', 'wrong-types', 'negative-tid',
             'huge-tid', 'duplicate-layers', 'malformed-protobuf', 'non-png', 'wrong-ihdr',
             'null-tile', 'wrong-tile-type', 'view', 'bad-config-schema', 'bad-decimal',
             'missing-ls', 'null-config', 'empty-layers', 'unknown-fields', 'bad-group',
             'overflow-varint', 'missing-id', 'max-id', 'wrong-wire', 'bad-key-type',
             'extra-column', 'generated-column', 'wal', 'large-config', 'without-rowid', 'nullable-config', 'nonnull-config',
             'collation-index', 'trigger', 'virtual-config', 'attach-schema',
             'decimal-rounding', 'decimal-tie', 'decimal-above-tie', 'grid-boundary', 'sqlite-prefix', 'utf8', 'invalid-utf8']
    for name in names:
        path = directory / (name + '.spd')
        path.unlink(missing_ok=True)
        con = sqlite3.connect(path)
        con.execute('PRAGMA page_size=4096')
        if name == 'bad-config-schema':
            con.execute('CREATE TABLE config(name TEXT, value BLOB)')
        elif name == 'extra-column':
            con.execute('CREATE TABLE config(name TEXT PRIMARY KEY NOT NULL,value BLOB,extra BLOB)')
        elif name == 'generated-column':
            con.execute('CREATE TABLE config(name TEXT PRIMARY KEY NOT NULL,value BLOB,extra AS (length(value)))')
        elif name == 'without-rowid':
            con.execute('CREATE TABLE config(name TEXT PRIMARY KEY,value BLOB) WITHOUT ROWID')
        elif name == 'nullable-config':
            con.execute('CREATE TABLE config(name TEXT PRIMARY KEY,value BLOB)')
        elif name == 'nonnull-config':
            con.execute('CREATE TABLE config(name TEXT PRIMARY KEY,value BLOB NOT NULL)')
        elif name == 'collation-index':
            con.execute('CREATE TABLE config(name TEXT COLLATE NOCASE PRIMARY KEY DESC,value BLOB) WITHOUT ROWID')
        else:
            con.execute('CREATE TABLE config(name TEXT PRIMARY KEY NOT NULL,value BLOB)')
        ls = layer(1, b'Layer 1') + layer(9999, b'Reference Layer', b'\x18\x02\x20\x01')
        if name == 'utf8': ls = layer(1, 'Слой é 日本 😀'.encode())
        if name == 'invalid-utf8': ls = layer(1, b'\xff\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80\xe2\x82')
        if name == 'duplicate-layers': ls += layer(1, b'Duplicate')
        if name == 'malformed-protobuf': ls = b'\x0a\x7f\x08'
        if name == 'empty-layers': ls = b''
        if name == 'unknown-fields':
            ls = layer(1, b'Layer 1', b'\x28\x01' + field(6, b'future') + b'\x3b\x08\x01\x3c') + b'\x40\x01'
        if name == 'bad-group': ls = layer(1, b'L', b'\x2b\x34')
        if name == 'overflow-varint': ls = field(1, b'\x08' + b'\xff' * 9 + b'\x02' + field(2, b'L'))
        if name == 'missing-id': ls = field(1, field(2, b'L'))
        if name == 'max-id': ls = layer(2**64 - 1, b'L')
        if name == 'wrong-wire': ls = field(1, b'\x0a\x00' + field(2, b'L'))
        config = {'fmt_ver': b'2', 'surface.width': b'1920.0', 'surface.height': b'2560.0',
                  'ppi': b'72', 'vp.x': b'249984.0', 'vp.y': b'249984.0', 'vp.scale': b'1.0',
                  'paperMode': b'3', 'template_name': b'', 'templateInfo': b'-1,-1,,',
                  'appVersion': b'synthetic', 'ls': ls, 'thumbnail': b'\xff\xd8\xff\xd9',
                  'templateData': b'', 'unknown.key': b'\0\xff', 'null.key': None,
                  'text.key': 'text', 'nul\0key': b'value'}
        if name == 'utf8': config['text.key'] = 'Слой é 日本 😀'
        if name == 'invalid-utf8':
            config['text.key'] = b'\xff'
            con.execute("INSERT INTO config VALUES(CAST(X'ff' AS TEXT), X'31')")
        if name == 'nonnull-config': del config['null.key']
        if name == 'decimal-rounding':
            config.update({'ppi': b'1.7976931348623157e308', 'vp.x': b'4.9406564584124654e-324',
                           'vp.y': b'-0', 'surface.width': b'2.2250738585072014e-308',
                           'vp.scale': b'0.1'})
        if name == 'decimal-tie': config['vp.x'] = b'1.00000000000000011102230246251565404236316680908203125'
        if name == 'decimal-above-tie': config['vp.x'] = b'1.00000000000000011102230246251565404236316680908203126'
        if name == 'unknown-version': config['fmt_ver'] = b'99'
        if name == 'bad-decimal': config['vp.x'] = b'nan'
        if name == 'missing-ls': del config['ls']
        if name == 'null-config': config['ppi'] = None
        if name == 'large-config': config['unknown.key'] = b'x' * 70000
        con.executemany('INSERT INTO config(name,value) VALUES (?,?)', config.items())
        if name == 'bad-key-type':
            con.execute('INSERT INTO config(name,value) VALUES (?,?)', (sqlite3.Binary(b'bad'), b'value'))
        if name == 'view': con.execute('CREATE VIEW surface_1 AS SELECT 0 AS tid, X\'00\' AS tile')
        elif name != 'missing-table':
            schema = 'tid INTEGER PRIMARY KEY NOT NULL,tile BLOB NOT NULL'
            if name == 'wrong-types': schema = 'tid TEXT PRIMARY KEY NOT NULL,tile BLOB NOT NULL'
            if name == 'null-tile': schema = 'tid INTEGER PRIMARY KEY NOT NULL,tile BLOB'
            con.execute('CREATE TABLE surface_1(' + schema + ') WITHOUT ROWID')
            # Unsorted, asymmetric extents catch transposition and ordering bugs.
            tids = [tile_id(1955, 1954), tile_id(1952, 1957), tile_id(1952, 1953)]
            if name == 'grid-boundary': tids = [4096, 4095, 0]
            if name == 'negative-tid': tids = [-1]
            if name == 'huge-tid': tids = [2**63 - 1]
            tile = png()
            if name == 'non-png': tile = b'not png'
            if name == 'wrong-ihdr': tile = png(64)
            if name == 'null-tile': tile = None
            if name == 'wrong-tile-type': tile = 'not a blob'
            con.executemany('INSERT INTO surface_1(tid,tile) VALUES (?,?)', ((tid, tile) for tid in tids))
        con.execute('CREATE TABLE surface_9999(tid INTEGER PRIMARY KEY NOT NULL,tile BLOB NOT NULL) WITHOUT ROWID')
        con.execute('CREATE TABLE future_table(something)')
        if name == 'sqlite-prefix':
            for table in ('sqlitex_user', 'sqliteXextra', 'sqlite1'):
                con.execute(f'CREATE TABLE {table}(value)')
        if name == 'collation-index':
            con.execute('CREATE INDEX tile_cover ON surface_1(tile,tid DESC)')
            con.execute('CREATE INDEX config_cover ON config(value,name COLLATE RTRIM)')
        if name == 'trigger':
            con.execute("CREATE TRIGGER hostile AFTER INSERT ON config BEGIN SELECT RAISE(ABORT,'must not execute'); END")
        if name in ('virtual-config', 'attach-schema'):
            con.execute('PRAGMA writable_schema=ON')
            sql = ('CREATE VIRTUAL TABLE config USING nonexistent_module' if name == 'virtual-config'
                   else "ATTACH DATABASE ':memory:' AS injected")
            con.execute("UPDATE sqlite_schema SET sql=? WHERE name='config'", (sql,))
        con.commit()
        con.close()
        if name == 'wal':
            data = bytearray(path.read_bytes())
            data[18:20] = b'\x02\x02'
            path.write_bytes(data)
    data = (directory / 'valid.spd').read_bytes()
    (directory / 'unordered-config.spd').write_bytes(data.replace(b'vp.x', b'vp.s'))
    (directory / 'duplicate-config.spd').write_bytes(data.replace(b'vp.x', b'vp.y'))
    (directory / 'truncated.spd').write_bytes(data[:5000])
    (directory / 'corrupt-header.spd').write_bytes(b'not SQLite' + data[10:])
    corrupt = bytearray(data)
    corrupt[4096] = 0xff
    (directory / 'corrupt-page.spd').write_bytes(corrupt)
    (directory / 'empty.spd').write_bytes(b'')
    for table in ('config', 'surface_1'):
        for depth in (4, 19):
            generate_btree(directory / f'dag-{table}-{depth}.spd', depth, table)
        generate_btree(directory / f'cycle-{table}.spd', 1, table, cycle=True)
        generate_btree(directory / f'cycle-deep-{table}.spd', 4, table, cycle=True)
    (directory / 'stamp').write_text('synthetic fixtures generated\n')


if __name__ == '__main__':
    generate(pathlib.Path(sys.argv[1]))
