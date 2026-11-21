/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef SNSPD_TYPES_H
#define SNSPD_TYPES_H
#include <stddef.h>
#include <stdint.h>

#include "snspd/export.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum spd_status : int {
  SPD_OK,
  SPD_ERR_INVALID_ARGUMENT,
  SPD_ERR_OUT_OF_MEMORY,
  SPD_ERR_UNSUPPORTED_FORMAT,
  SPD_ERR_UNEXPECTED_EOF,
  SPD_ERR_MALFORMED,
  SPD_ERR_LIMIT,
} spd_status;
typedef struct spd_error {
  spd_status status;
  /* SQLite result code for engine failures, zero otherwise. */
  int64_t value;
  /* Fixed description, never drawing content or SQLite error text. */
  char message[256];
} spd_error;
typedef struct spd_string {
  /* Raw bytes, NUL-terminated; length preserves embedded NULs. No UTF-8 repair.
   */
  const char* data;
  size_t length;
} spd_string;
typedef struct spd_bytes {
  const uint8_t* data;
  size_t size;
} spd_bytes;
typedef struct spd_config_entry {
  spd_string key;
  spd_bytes value;
  /* SQLite NULL is distinct from an empty BLOB or TEXT. */
  bool is_null;
  bool is_text;
} spd_config_entry;
typedef struct spd_tile {
  int64_t tid;
  /* Column-major: tid = tx*4096 + ty. Keep the quotient at full width. */
  uint64_t tx;
  uint32_t ty;
  /* Owned by the drawing, undecoded. */
  spd_bytes png;
  /* Signature and IHDR only; CRCs, pixels and compression are not validated. */
  bool has_expected_ihdr;
} spd_tile;
typedef struct spd_layer {
  uint64_t id;
  spd_string name;
  uint64_t field3;
  uint64_t field4;
  bool has_field3;
  bool has_field4;
  size_t unknown_field_count;
  /* Complete original protobuf submessage, including unknown/repeated fields.
   */
  spd_bytes raw;
  /* Ascending tid, equivalently (tx, ty): column-major. */
  const spd_tile* tiles;
  size_t tile_count;
} spd_layer;
typedef struct spd_rect {
  double x, y, width, height;
  bool is_null;
} spd_rect;
typedef struct spd_canvas {
  /* A presence bit distinguishes missing metadata from a stored zero. */
  double width, height, ppi;
  double viewport_x, viewport_y, viewport_scale;
  bool has_width, has_height, has_ppi;
  bool has_viewport_x, has_viewport_y, has_viewport_scale;
  spd_string fmt_ver, paper_mode, template_name, template_info, app_version;
  spd_bytes thumbnail, template_data;
} spd_canvas;
typedef struct spd_drawing {
  spd_canvas canvas;
  const spd_layer* layers;
  size_t layer_count;
  /* Binary key order, independent of SQLite row order or collation. */
  const spd_config_entry* config;
  size_t config_count;
  size_t extra_table_count;
  size_t unknown_layer_field_count;
  size_t invalid_tile_count;
  bool unknown_format;
  /* WAL header flags normalized in the private snapshot. No sidecar is read. */
  bool had_wal_header;
  /* Input is copied. All views live until spd_drawing_free. Treat this entire
   * transparent model as read-only. No SQLite connection survives open. */
} spd_drawing;

SPD_API const char* spd_status_string(spd_status status);
SPD_API void spd_drawing_free(spd_drawing* drawing);
/* Exact byte lookup, including embedded NULs. Null when absent/invalid. */
SPD_API const spd_config_entry* spd_config_find(const spd_drawing* drawing,
                                                const char* key, size_t length);
/* Geometry subtracts the saved viewport: tx*128-vp.x, ty*128-vp.y. Whether
 * that viewport is the page centre or top-left is not established. Its scale
 * is exposed but not applied. Missing origins default to zero. Large integer
 * coordinates may round to double precision. Null arguments yield null rects.
 */
SPD_API spd_rect spd_tile_rect(const spd_drawing* drawing,
                               const spd_tile* tile);
/* Extent of stored tiles, including fully transparent tiles; no pixel decoding.
 * layer may be null to include every layer. Empty extent is a null rectangle.
 */
SPD_API spd_rect spd_painted_bounds(const spd_drawing* drawing,
                                    const spd_layer* layer);
#ifdef __cplusplus
}
#endif
#endif
