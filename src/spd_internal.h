/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef SPD_INTERNAL_H
#define SPD_INTERNAL_H
#include <stdalign.h>
#include <stdlib.h>

#include "snspd/snspd.h"

typedef struct spd_meter {
  size_t live;
  size_t ceiling;
  uint64_t work;
  spd_status status;
} spd_meter;
typedef union spd_block spd_block;
union spd_block {
  max_align_t alignment;
  struct {
    size_t size;
    spd_block* next;
  } h;
};
typedef struct spd_owner {
  spd_drawing drawing;
  spd_block* blocks;
} spd_owner;
void spd__internal_log_write(spd_log_level level, const char* message);
bool spd__internal_charge(spd_meter* meter, uint64_t work);
void* spd__internal_alloc(spd_meter* meter, size_t size);
void spd__internal_dealloc(spd_meter* meter, void* data);
void* spd__internal_keep(spd_owner* owner, spd_meter* meter, size_t size);
#endif
