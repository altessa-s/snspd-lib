/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* The process-wide diagnostics sink is silent until installed. Messages
 * describe failures and unfamiliar fields without including drawing text, paths
 * or names. The callback runs on the parsing thread and must synchronize shared
 * state. */

#ifndef SNSPD_LOG_H
#define SNSPD_LOG_H

#include "snspd/export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DEBUG reports unfamiliar fields or image headers retained in a model;
 * ERROR reports failure to open the entire drawing. */
typedef enum spd_log_level : int {
  /* An unfamiliar format detail was read past without losing a record. */
  SPD_LOG_DEBUG,
  /* The entire drawing could not be opened. */
  SPD_LOG_ERROR,
} spd_log_level;

/* What a host installs. Called on whichever thread parsed the drawing — the
 * parser runs wherever the work is, and a sink that touches shared state must
 * say so itself. `message` is NUL-terminated and is not owned by the sink: it
 * lives only for the duration of the call. The sink must return normally: it
 * must not unwind out of the library through longjmp, a C++ exception or a
 * panic from another runtime, or storage of the parse in progress is lost. */
typedef void (*spd_log_sink)(spd_log_level level, const char* message,
                             void* context);

/* Name the sink for this process. Once, at its start, from the composition
 * root; a later call replaces the earlier one. Null sink silences logging.
 * Install/silence may race with parsing. Keep contexts alive until all parsing
 * threads that could have seen them finish; silence does not wait for
 * callbacks. Allocation failure leaves the old sink installed. Retired bindings
 * remain retained for process lifetime so in-flight callbacks cannot use freed
 * storage. Reinstalling a retained sink/context pair allocates nothing. Storage
 * grows only with distinct pairs: three pointer-sized fields (normally 24 bytes
 * on 64-bit hosts), plus allocator overhead, for the life of the process.
 * Concurrent installs of the same new pair may retain duplicate bindings.
 * Null installs and silence allocate nothing and reclaim nothing. A fresh
 * context per install still grows storage; install once at startup and filter
 * in the sink. */
SPD_API void spd_log_install(spd_log_sink sink, void* context);

/* Go back to silence. For a test that installed a sink and must not leave it
 * behind for the next one. */
SPD_API void spd_log_silence(void);

#ifdef __cplusplus
}
#endif

#endif /* SNSPD_LOG_H */
