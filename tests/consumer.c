/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <snspd/snspd.h>
#include <stdio.h>
int main(void) {
  spd_options options = spd_options_default();
  printf("%s\n", SNSPD_VERSION_STRING);
  return options.limits.max_layers > 0 &&
                 spd_tile_rect(nullptr, nullptr).is_null
             ? 0
             : 1;
}
