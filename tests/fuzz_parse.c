/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <stdlib.h>

#include "snspd/snspd.h"
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  spd_options options = spd_options_default();
  options.limits.max_file_size = 8u * 1024u * 1024u;
  options.limits.max_memory_bytes = 32u * 1024u * 1024u;
  options.limits.max_work = 2u * 1024u * 1024u;
  options.strict = size != 0 && (data[size - 1] & 1u) != 0;
  options.require_png_128 = size != 0 && (data[size - 1] & 2u) != 0;
  spd_drawing* drawing = nullptr;
  spd_error error;
  spd_status status = spd_open(data, size, &options, &drawing, &error);
  if (status != error.status) abort();
  if (status == SPD_OK) {
    if (drawing == nullptr) abort();
    (void)spd_painted_bounds(drawing, nullptr);
    for (size_t i = 0; i < drawing->layer_count; i++) {
      const spd_layer* l = &drawing->layers[i];
      (void)spd_painted_bounds(drawing, l);
      for (size_t j = 0; j < l->tile_count; j++)
        (void)spd_tile_rect(drawing, &l->tiles[j]);
    }
    for (size_t i = 0; i < drawing->config_count; i++) {
      const spd_config_entry* e = &drawing->config[i];
      if (spd_config_find(drawing, e->key.data, e->key.length) != e) abort();
    }
  } else if (drawing != nullptr)
    abort();
  spd_drawing_free(drawing);
  return 0;
}
