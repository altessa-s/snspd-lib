# SNSpd (C23)

Reading Supernote Atelier `.spd` drawings: canvas metadata, ordered layers and
stored PNG tiles. PNG/JPEG decoding and composition belong to a renderer such
as snrender-lib. The library never writes drawings.

A C23 compiler with typed enums and `nullptr` is required (clang 19+, GCC 13+;
Windows uses clang with the Windows SDK). CMake 3.21+ builds the library;
Python 3.9+ with sqlite3 and zlib generates synthetic tests. The official SQLite
amalgamation is vendored; builds require no download or system SQLite.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSNSPD_WERROR=ON
cmake --build build --parallel
cmake --build build --target check
cmake --install build --prefix /usr/local
```

| Option | Default | Purpose |
|---|---|---|
| `BUILD_SHARED_LIBS` | OFF | Shared library |
| `SNSPD_BUILD_TESTS` | On at top level | Synthetic tests and consumers |
| `SNSPD_BUILD_TOOLS` | On at top level | `spddump` |
| `SNSPD_WERROR` | OFF | Warnings as errors, enabled in CI |
| `SNSPD_LTO` | ON | Shared Release optimization when available |
| `SNSPD_BUILD_FUZZERS` | OFF | Clang libFuzzer parser harness |

```cmake
set(CMAKE_C_STANDARD 23)
find_package(SNSpd REQUIRED)
target_link_libraries(yours PRIVATE SNSpd::snspd)
```

```sh
cc -std=c2x yours.c $(pkg-config --cflags --libs --static snspd)
```

```c
#include <snspd/snspd.h>

spd_drawing* drawing = nullptr;
spd_error error;
if (spd_open(data, size, nullptr, &drawing, &error) == SPD_OK) {
    for (size_t i = 0; i < drawing->layer_count; i++) {
        const spd_layer* layer = &drawing->layers[i];
        /* layer->name, layer->id, layer->tiles[j].png */
    }
    spd_drawing_free(drawing);
}
```

Input is copied temporarily and released before open returns; no SQLite
connection survives open. Arrays, strings, raw config
and encoded image bytes are borrowed from the drawing and remain valid until
`spd_drawing_free` (which accepts null). Treat transparent structures as read-only.
Failure leaves the output pointer unchanged; the optional error has a status,
SQLite result code for engine failures (zero otherwise) and a fixed-size, content-free message. There are
no allocator hooks, matching snnote-lib.

Strings preserve stored bytes with a NUL terminator and explicit byte length,
including embedded NULs. This first reader does not repair or assume UTF-8.
`spd_config_find` looks up exact byte keys; raw values preserve TEXT/BLOB/NULL
storage distinctions. Config entries are in binary key order. Layers keep
protobuf order. Tiles are sorted by ascending `tid`, equivalently `(tx, ty)`
(column-major): `tid = tx*4096 + ty`, so `tx = tid/4096`, `ty = tid%4096`.
The quotient `tx` is 64-bit; `ty` is 32-bit and ranges from 0 to 4095.

Only one real Atelier export is available. Renderer comparison with its embedded
JPEG supports this mapping: the previous axes with saved viewport/canvas gave
MAE 11.93 and correlation -0.019; corrected axes with painted bounds gave MAE
0.97 and correlation 0.981. That comparison also changes framing; it does not
establish viewport semantics or behavior across other exports. See the
[evidence and viewport analysis](docs/format.md#tile-grid).

| Status | Meaning |
|---|---|
| `SPD_OK` | Complete model, possibly with unfamiliar metadata or image headers |
| `SPD_ERR_INVALID_ARGUMENT` | Null required pointer |
| `SPD_ERR_OUT_OF_MEMORY` | Host allocation failed |
| `SPD_ERR_UNSUPPORTED_FORMAT` | Unsupported container/header, or strict fmt_ver |
| `SPD_ERR_UNEXPECTED_EOF` | Input too short for the SQLite header |
| `SPD_ERR_MALFORMED` | Invalid SQLite/schema/protobuf/scalar/tile structure |
| `SPD_ERR_LIMIT` | Size, memory or work ceiling exceeded |

`spd_options_default()` provides configurable limits; pass options to `spd_open`.
Zero is a zero ceiling, not a request for defaults. `strict` rejects versions
other than `fmt_ver="2"`; default mode accepts and flags unknown or missing
versions. Unknown config keys, extra tables and protobuf fields are retained or
counted. Malformed required structures reject the whole drawing. Missing canvas
numbers have explicit presence flags; dimensions/ppi/scale, if supplied, must
be finite positive ASCII decimals (at most 128 bytes, correctly rounded by
C-locale `strtod_l` / `_strtod_l`, independent of the process locale). Viewport
coordinates may be negative.

`require_png_128` optionally rejects unexpected PNG signature/IHDR dimensions;
default mode retains and counts mismatches. Neither mode validates image CRCs,
compression, bit depth, color type or pixels. `spd_tile_rect` and
`spd_painted_bounds` use stored pixels relative to viewport x/y, without applying
scale. Bounds include fully transparent stored tiles; actual painted pixels
cannot be determined without decoding. Empty bounds are null rectangles.
These helpers subtract the saved `vp.x`/`vp.y`; they do not establish a page
top-left. In the single sample, centering the 1920×2560 page on that saved point
contains all 143 stored tiles; treating it as top-left overlaps only 29.
Centre is therefore more plausible, but remains an inference. No half-page
offset or clipping is applied by the reader.

| Default ceiling | Value |
|---|---|
| File bytes | 256 MiB |
| Layers | 256 |
| Tiles per layer | 262,144 |
| Individual / total tile bytes | 4 MiB / 256 MiB |
| Config entries / combined key and value bytes | 4,096 / 16 MiB |
| Live heap, including SQLite, copied input and allocation headers | 768 MiB |
| Work units | 1,073,741,824 (1 GiB) |

SQLite also imposes a fixed 64 MiB ceiling on each value and encoded row
(including row overhead). Raising caller limits does not lift this engine
ceiling; exceeding it returns `SPD_ERR_LIMIT`.

Work charges each copied input or retained allocation byte once, covering its
zeroing and input or payload copy; it also charges examined bytes in reader code,
allocation requests, protobuf traversal, layer-ID comparisons, lookups and
SQLite VM instructions (progress interval 1, including preparation). It is a
work meter, not a CPU instruction or wall-clock guarantee: SQLite's internal
operations between VM callbacks are additionally bounded by memory, SQL, schema,
column and record limits. All row counts stream a VM step per row. Queries
request `NOT INDEXED`; SQLite can still choose covering indexes for WITHOUT
ROWID tables. Config and tile sorting use a metered heap sort in reader code,
with independent duplicate checks. No reader query uses a count aggregate,
min/max optimization or SQL sorter that could hide a full traversal inside
one opcode. B-tree descent is bounded by SQLite's depth limit; cyclic children
fail as malformed. Work exhaustion also interrupts the connection.

The default work ceiling is four times the default file ceiling: one input
copy, retained payload/model bytes, and room for traversal and comparisons.
Changing a byte ceiling may require raising `max_work` and `max_memory_bytes`;
a file below one ceiling can exceed another. The private SQLite phase is
serialized, including input snapshot allocation and copying, so waiting opens
do not retain additional snapshots. Lock waiting and host callbacks are outside
the work budget. Waiting uses a blocking pthread mutex on POSIX and an SRW lock
on Windows. After `fork()` in a multithreaded process, the child must `exec()`
before calling this library (including logging); it must not reuse inherited
library state or locks. Heap accounting excludes malloc implementation overhead,
stack, caller input, bounded C-locale conversion state, and process-lifetime
diagnostic bindings.

SQLite uses readonly deserialization, defensive/untrusted-schema modes,
disabled triggers/views/foreign keys, bounded SQL limits and a VFS that refuses
file I/O. A private allocator avoids affecting the application's SQLite or its
process-global heap limit. WAL flags are normalized only in the input
copy and reported as `had_wal_header`; the supplied bytes must be a complete
snapshot. Sidecar transactions cannot be read or detected. See
[vendor details](vendor/sqlite/README.md).

Diagnostics match snnote-lib: `spd_log_install` publishes a process-wide sink
and context atomically; `spd_log_silence` restores silence. Callbacks run on the
calling thread after releasing SQLite state and may reenter the library. They
receive fixed descriptions, never drawing content. Keep contexts alive until
all possible callbacks finish. Distinct installed bindings remain retained for
process lifetime (three pointer-sized fields plus allocator overhead); repeated
pairs are reused. Concurrent first installations can retain duplicate bindings.
Install once at startup. Callbacks must return normally.

```sh
spddump drawing.spd
spddump --json drawing.spd
spddump --require-png-128 drawing.spd
spddump --thumbnail thumbnail.jpg drawing.spd
```

The dumps are deterministic UTF-8. Valid UTF-8 names and keys are JSON strings;
invalid UTF-8 names and keys are objects of the form `{"hex":"ff00"}` in both
output modes, preserving bytes without presenting them as Unicode. This changes
the CLI representation of invalid strings; the C API still preserves all bytes.
Config values are printed only when valid UTF-8 without ASCII control
characters, except `ls`, `thumbnail` and `templateData`, whose sizes alone are
shown. JSON includes grid extents, tile byte totals and min/max sizes. Text
includes viewport-relative bounds. No tile or thumbnail image data is dumped.
Thumbnail extraction exclusively creates a new file and refuses to overwrite
any existing file.

The exact public export set is nine functions; static archive helpers use the
reserved `spd__internal_` prefix. Public functions are declared with `SPD_API`
in the public headers. Version comes only from `SNSPD_VERSION_MAJOR/MINOR/PATCH` in
`include/snspd/snspd.h`. CMake and relocatable pkg-config metadata derive it.
Until 1.0, ABI compatibility is limited to one minor version.

Layout follows snnote-lib: `include/snspd/` is the public contract; `src/` holds
model, private SQLite reader and atomic logging; `cmake/` holds package metadata;
`tests/` generates synthetic fixtures, tests hostile inputs/ABI/concurrency,
consumers and dumps; `tools/spddump.c` is the CLI. No private sample or image
payload is stored in the repository. CI covers Linux/macOS/Windows, static and
shared builds, ASan/UBSan, exact exports, installed consumers and clang-format
21.1.8. Nightly libFuzzer retains crash, leak, timeout, OOM and slow-unit artifacts.

Known format facts and open questions are in [docs/format.md](docs/format.md).
MIT — see [LICENSE](LICENSE) and [COPYRIGHT](COPYRIGHT). Vendored SQLite is
public domain with its own provenance.

## Trademarks

Supernote and Supernote Atelier are trademarks of Ratta Software. This project is not affiliated with, endorsed by or sponsored by Ratta Software.
