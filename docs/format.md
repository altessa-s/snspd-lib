# Supernote Atelier `.spd` format

This is an initial reader based on one self-contained export from Atelier
1.1.82. Synthetic fixtures test parser behavior; they provide no additional
evidence about the application's semantics. The observations below are not a
vendor specification.

## SQLite container

The file begins with `SQLite format 3\0`. Observed schema:

```sql
CREATE TABLE config(name TEXT PRIMARY KEY NOT NULL, value BLOB);
CREATE TABLE surface_1(tid INTEGER PRIMARY KEY NOT NULL, tile BLOB NOT NULL) WITHOUT ROWID;
CREATE TABLE surface_9999(tid INTEGER PRIMARY KEY NOT NULL, tile BLOB NOT NULL) WITHOUT ROWID;
```

SQLite's dynamic storage classes matter: despite the BLOB declaration, some
config values (including the JPEG thumbnail) have TEXT storage class. Read
values as bytes, never assume valid Unicode. NULL and empty TEXT/BLOB are
distinct. The sample also has a NULL `frames` config entry.

The reader checks real table objects with positive root pages and exactly the
two expected ordinary columns, declared types (case-insensitive), primary-key
positions and NOT NULL constraints before querying their content. It accepts
rowid or WITHOUT ROWID tables with this column contract. Extra columns and
generated columns on required tables reject the drawing; unrelated tables are
counted and ignored. Views/virtual tables cannot replace required tables.
Returned config keys are independently checked for strict binary ordering and
uniqueness: a corrupt index cannot invalidate the raw-key lookup contract.
Runtime storage classes are checked independently of declared types. Config
values accept TEXT/BLOB/NULL; tile rows require INTEGER tid and BLOB tile.

WAL read/write flags were observed in the exported database header. SQLite's
readonly deserialize interface needs rollback flags, so the reader changes
bytes 18/19 only in its padded private copy and sets `had_wal_header`. It never
opens WAL files, attaches another database or recovers sidecar transactions.
Only the supplied pages are available: callers must provide a complete snapshot.
A missing uncheckpointed transaction cannot be detected from this input alone.

Only fixed SQL or a `surface_` name formatted from an unsigned integer bounded
to INT64_MAX reaches SQL preparation. No table names or SQL fragments taken
from file content are interpolated.

## Config

| Key | Observed interpretation |
|---|---|
| `fmt_ver` | ASCII format version, observed `2` |
| `submit_count` | ASCII counter, semantics not established; raw only |
| `surface.width`, `surface.height` | Canvas/page pixel dimensions |
| `ppi` | Stored pixel density |
| `vp.x`, `vp.y`, `vp.scale` | Stored viewport point and scale; page anchoring unconfirmed |
| `paperMode` | Stored mode code, exposed as text without assigning an enum |
| `template_name`, `templateInfo` | Stored template metadata; info syntax uninterpreted |
| `appVersion` | App version string |
| `ls` | Protobuf layer-list container |
| `thumbnail` | Encoded JPEG in this sample; undecoded bytes |
| `templateData` | Empty in the sample; uninterpreted bytes |
| `frames` | NULL in the sample; semantics unknown |

Known numbers accept locale-independent ASCII decimals (optional sign,
fraction, exponent); whitespace, embedded NULs, nan/inf and hexadecimal spellings
are invalid. Exponent magnitude is at most 308 and spelling at most 128 bytes.
Dimensions, density and scale must be positive when present. Missing/NULL
numbers have false presence flags and zero values. Other absent text fields
have null data pointers; empty stored fields have a non-null pointer and length
zero. Unknown keys and non-numeric text values remain exact bytes.

Unknown or missing fmt_ver is accepted and flagged in default mode. Strict mode
requires exactly one byte `2`; it still accepts unknown config keys, extra tables
and protobuf fields. An unknown version must otherwise satisfy this reader's
required schema and layer-list shape.

## Layer protobuf

The container's field 1 is a repeated length-delimited layer message:

| Layer field | Wire type | Interpretation |
|---|---|---|
| 1 | varint | Layer id; `surface_<id>` names its table |
| 2 | bytes | Layer name |
| 3 | varint if present | Exposed raw, no semantic claim |
| 4 | varint if present | Exposed raw, no semantic claim |

Layers preserve message order, including empty layers. Names preserve all bytes
and are NUL-terminated in the model. Fields 1 and 2 are required; repeated known
scalar/name fields follow protobuf's last-value rule. IDs above INT64_MAX and
duplicate final IDs reject the drawing. Numeric IDs are formatted as canonical
decimal digits in SQL. Every listed layer must have its required table.

The sample's two layer messages are 11 and 20 bytes respectively. The reference
layer's 20 bytes end immediately after its name. The following `10 02 18 01`
are **top-level** fields 2 and 3, rather than members of that layer message.
Their semantics are unknown. No evidence establishes visibility, ordering,
active-layer selection or layer type for these flags. The original `ls` config
preserves the complete container; each layer also exposes its raw submessage.
`unknown_layer_field_count` includes both container and child unknown fields.

Unknown wire types 0, 1, 2 and 5 are skipped with bounds checks. Deprecated
protobuf groups (types 3/4) are skipped with matching-field checks and at most
32 nested groups. Invalid tags, impossible lengths, varint overflow, unmatched
groups and wrong wire types for known fields reject the drawing. Non-minimal
but bounded protobuf varints are accepted. Unknown fields are never interpreted.

## Tile grid

Each row contains one undecoded PNG. The observed tiles have 128×128 IHDRs,
8-bit **grayscale with alpha** (PNG color type 4), rather than RGBA (type 6).
The library imposes neither color type nor bit depth. The optional check tests
only the PNG signature, first IHDR chunk length/type and 128×128 dimensions,
requiring at least the complete 33-byte signature/IHDR/CRC region. It does not
validate the CRC, other IHDR fields, chunk structure, decompression or pixels.
Default mode retains and counts header mismatches; `require_png_128` rejects
them. This library cannot establish that an image is decodable or nontransparent.

```text
tid = tx * 4096 + ty
tx = tid / 4096
ty = tid % 4096
x = tx * 128 - vp.x
y = ty * 128 - vp.y
```

Only nonnegative signed SQLite INTEGER IDs are accepted. INT64_MAX is supported
without multiplication in signed integer arithmetic. Thus ty is 0…4095; tx may
be much larger (up to 2251799813685247). The model uses uint64_t for tx and
uint32_t for ty. Negative tid semantics are unknown and rejected. Tiles sort by
ascending tid, equivalently (tx, ty): column-major. Geometry uses doubles, which
can round extremely large coordinates; bounds dimensions are calculated from
integer coordinate spans before conversion so a single large tile still spans
128 pixels.

The axis mapping corrects an earlier transposed interpretation. snrender-lib
rendered the one available real export and compared an aspect-preserving,
centered fit with its embedded 600×800 JPEG thumbnail:

| Mapping and framing | MAE (0–255) | Correlation |
|---|---:|---:|
| Previous `tx=tid%4096`, `ty=tid/4096`; saved viewport/canvas | 11.93 | -0.019 |
| Corrected column-major axes; painted bounds | 0.97 | 0.981 |

The matching layout supports the corrected axes. The numerical comparison also
changes framing, so it is not an isolated axis-only experiment and cannot
establish the saved viewport's meaning. No second real sample exists in this
investigation; synthetic fixtures do not validate format semantics.

Read-only numeric inspection of that same sample establishes:

- Layer 1 has 143 stored tiles, tx 1946…1958 and ty 1945…1962 inclusive;
  the reference layer is empty.
- Stored tile bounds in absolute pixels are [249088,250752) × [248960,251264),
  width 1664 and height 2304 (13×18 tiles).
- vp.x = vp.y = 249984, or tile coordinate (1953,1953), and vp.scale = 1.
  Relative to that point, stored bounds are (-896,-1024,1664,2304).
  The point is 7 tiles from the left and 8 from the top of those bounds;
  their centre is (1952.5,1954), offset (-64,+128) pixels from vp.
- The canvas is 1920×2560 pixels (15×20 tiles). If vp is its top-left, the page
  spans [1953,1968) × [1953,1973) in tile coordinates, overlapping only 29 of
  143 stored tiles; 114 lie outside.
- If vp is its centre, the page spans [1945.5,1960.5) × [1943,1963), or
  [249024,250944) × [248704,251264) pixels. All 143 stored tiles fit fully
  inside; margins around their bounds are left 64, right 192, top 256,
  bottom 0 pixels.

Centre is more plausible than page top-left for this sample, but remains an
inference: tiles can be transparent or outside the intended page, and thumbnail
framing need not follow the saved viewport. The reader makes no page-anchor
claim. Geometry helpers retain the explicit subtraction above, accept fractional
viewport coordinates, expose `vp.scale` without applying it, and perform no
half-page offset, clipping or deletion. Extents describe stored tile rectangles,
including potentially transparent tiles, not nontransparent ink.

## Limits and unknowns

See [README](../README.md) and public `parser.h` for configurable file, layer,
tile, config, live-memory and work ceilings. SQLite also has hard build limits
on record length (64 MiB), SQL length (4096), columns (32), expression depth (32)
and parameters (8). Raising caller limits cannot override these engine limits.
Schema parsing, statement preparation and execution share the progress meter;
internal SQLite operations between callbacks are bounded by these size limits.

Open questions require additional independent files: layer flag meanings and
whether fields 3/4 occur inside layer messages; top-level protobuf semantics;
other format versions; negative tile ids; reference layer behavior; template and
frames payloads; viewport page anchoring, scaling and coordinate units across
device/export settings; other image encodings and tile dimensions; transactional
guarantees of exports carrying WAL flags. No UI visibility or compositing order
is inferred.
