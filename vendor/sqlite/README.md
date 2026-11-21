# SQLite amalgamation

Official, unmodified `sqlite3.c` and `sqlite3.h`, public domain. See [SQLite's
copyright statement](https://www.sqlite.org/copyright.html). Download provenance
and both archive hashes are in [VERSION](VERSION). Version selected from the
[official download page](https://www.sqlite.org/download.html).

The build includes the amalgamation in `src/spd_parser.c` as a system header,
with `SQLITE_API` and `SQLITE_EXTERN` set to static. No SQLite symbol or header
is exported or installed, including when snspd is a static archive. No system
SQLite dependency is required, and the host's SQLite is never configured.

Compile options are kept in CMakeLists.txt: OS_OTHER, THREADSAFE=0,
DEFAULT_MEMSTATUS=0, ZERO_MALLOC, OMIT_LOAD_EXTENSION, DQS=0, OMIT_WAL,
OMIT_SHARED_CACHE, OMIT_UTF16, OMIT_DEPRECATED, OMIT_TCL_VARIABLE, OMIT_TRACE,
TEMP_STORE=3, DEFAULT_LOOKASIDE=0,0, ENABLE_API_ARMOR, MAX_LENGTH=67108864,
MAX_SQL_LENGTH=4096, MAX_COLUMN=32, MAX_EXPR_DEPTH=32, MAX_VARIABLE_NUMBER=8.

The only VFS refuses every file open/delete/path resolution. It supplies fixed
randomness and time; drawing parsing does not use SQL randomness or dates.
Extension loading is compiled out. No OS VFS is compiled. No temp file, WAL or
sidecar is ever opened.

One atomic lock serializes this private engine's initialization, use and
shutdown. Before initialization for each open, SQLITE_CONFIG_MALLOC installs
an allocator tied to that parse's meter. All SQLite allocations, model storage,
input and allocation headers share its live memory ceiling. Reallocation counts
both old and new allocations until the copy completes. SQLite is shut down
before the meter goes out of scope. No process-global hard heap limit is used.
No host callbacks run under the lock. Lock wait time, host allocation overhead,
stack, and retained log bindings are outside the budget.

The input copy has 32 zero padding bytes, following the
[deserialize guidance](https://www.sqlite.org/c3ref/deserialize.html) for hostile
buffers. For WAL-header databases, bytes 18/19 are normalized to 1 in this copy
as documented by SQLite; the model reports `had_wal_header`. Only pages in the
supplied bytes are read. Callers must provide a self-contained, checkpointed
snapshot: uncheckpointed WAL transactions are unavailable and cannot be detected
or recovered here. The caller's input is never modified.
