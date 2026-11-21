/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <stddef.h>

#include "snspd/snspd.h"
static_assert(sizeof(spd_status) == sizeof(int));
static_assert(sizeof(spd_log_level) == sizeof(int));
static_assert(offsetof(spd_error, status) == 0);
static_assert(sizeof(((spd_error*)0)->message) == 256);
static_assert(sizeof(((spd_layer*)0)->id) == 8);
static_assert(offsetof(spd_drawing, canvas) == 0);
int main(void) {
  const char* (*status)(spd_status) = spd_status_string;
  spd_status (*open)(const void*, size_t, const spd_options*, spd_drawing**,
                     spd_error*) = spd_open;
  spd_options (*defaults)(void) = spd_options_default;
  void (*release)(spd_drawing*) = spd_drawing_free;
  const spd_config_entry* (*find)(const spd_drawing*, const char*, size_t) =
      spd_config_find;
  spd_rect (*rect)(const spd_drawing*, const spd_tile*) = spd_tile_rect;
  spd_rect (*bounds)(const spd_drawing*, const spd_layer*) = spd_painted_bounds;
  void (*install)(spd_log_sink, void*) = spd_log_install;
  void (*silence)(void) = spd_log_silence;
  return status == nullptr || open == nullptr || defaults == nullptr ||
         release == nullptr || find == nullptr || rect == nullptr ||
         bounds == nullptr || install == nullptr || silence == nullptr;
}
