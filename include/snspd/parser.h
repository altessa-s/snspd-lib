/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef SNSPD_PARSER_H
#define SNSPD_PARSER_H
#include "snspd/types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct spd_limits {
  size_t max_file_size;
  size_t max_layers;
  size_t max_tiles_per_layer;
  size_t max_tile_bytes;
  size_t max_total_tile_bytes;
  size_t max_config_entries;
  size_t max_config_bytes;
  /* Live allocation bytes, including headers, copied input, SQLite and model.
   * Work charges retained bytes once (zeroing/copy), examined bytes, row and
   * protobuf steps, SQLite VM steps
   * (progress interval 1), allocation requests, and layer-id comparisons. */
  size_t max_memory_bytes;
  uint64_t max_work;
} spd_limits;
typedef struct spd_options {
  spd_limits limits;
  /* Strict rejects fmt_ver other than "2". Unknown keys/tables/fields remain
   * accepted and available even in strict mode. */
  bool strict;
  /* Reject unexpected signature/IHDR; otherwise count and retain each tile. */
  bool require_png_128;
} spd_options;

/* Default: 256 MiB file, 256 layers, 262144 tiles/layer, 4 MiB/tile,
 * 256 MiB total tile bytes, 4096 config entries, 16 MiB config bytes,
 * 768 MiB live memory, 1073741824 work units (4 * file ceiling, including
 * copy/model/traversal). Zero is an actual zero limit. Ceilings are
 * independent. SQLite additionally limits each value and encoded row to
 * 64 MiB (including row overhead), even when caller ceilings are higher.
 * Exceeding this engine limit returns SPD_ERR_LIMIT. */
SPD_API spd_options spd_options_default(void);
/* Copies input. Output remains unchanged on every failure. error is optional,
 * and is reset on success. options may be null for defaults. Independent calls
 * are safe across threads; the private SQLite phase is serialized. Input must
 * remain readable and unchanged for the duration of this call. After fork in
 * a multithreaded POSIX process, the child must exec before using this library.
 */
[[nodiscard]] SPD_API spd_status spd_open(const void* data, size_t size,
                                          const spd_options* options,
                                          spd_drawing** output,
                                          spd_error* error);
#ifdef __cplusplus
}
#endif
#endif
