/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <math.h>
#include <string.h>

#include "spd_internal.h"

bool spd__internal_charge(spd_meter* meter, uint64_t work) {
  if (work > meter->work) {
    meter->status = SPD_ERR_LIMIT;
    return false;
  }
  meter->work -= work;
  return true;
}
void* spd__internal_alloc(spd_meter* meter, size_t size) {
  if (!spd__internal_charge(meter, 1)) return nullptr;
  if (size > SIZE_MAX - sizeof(spd_block) ||
      sizeof(spd_block) + size > meter->ceiling - meter->live) {
    meter->status = SPD_ERR_LIMIT;
    return nullptr;
  }
  spd_block* block = malloc(sizeof *block + size);
  if (block == nullptr) {
    meter->status = SPD_ERR_OUT_OF_MEMORY;
    return nullptr;
  }
  block->h = (typeof(block->h)){.size = size};
  meter->live += sizeof *block + size;
  return block + 1;
}
void spd__internal_dealloc(spd_meter* meter, void* data) {
  if (data == nullptr) return;
  spd_block* block = (spd_block*)data - 1;
  meter->live -= sizeof *block + block->h.size;
  free(block);
}
void* spd__internal_keep(spd_owner* owner, spd_meter* meter, size_t size) {
  if (!spd__internal_charge(meter, size)) return nullptr;
  void* data = spd__internal_alloc(meter, size);
  if (data != nullptr) {
    spd_block* block = (spd_block*)data - 1;
    block->h.next = owner->blocks;
    owner->blocks = block;
    memset(data, 0, size);
  }
  return data;
}
void spd_drawing_free(spd_drawing* drawing) {
  if (drawing == nullptr) return;
  spd_owner* owner = (spd_owner*)drawing;
  spd_block* block = owner->blocks;
  while (block != nullptr) {
    spd_block* next = block->h.next;
    free(block);
    block = next;
  }
  free((spd_block*)owner - 1);
}
const char* spd_status_string(spd_status status) {
  switch (status) {
    case SPD_OK:
      return "success";
    case SPD_ERR_INVALID_ARGUMENT:
      return "invalid argument";
    case SPD_ERR_OUT_OF_MEMORY:
      return "out of memory";
    case SPD_ERR_UNSUPPORTED_FORMAT:
      return "unsupported format";
    case SPD_ERR_UNEXPECTED_EOF:
      return "unexpected end of input";
    case SPD_ERR_MALFORMED:
      return "malformed drawing";
    case SPD_ERR_LIMIT:
      return "memory, size or work budget exceeded";
    default:
      return "unknown status";
  }
}
const spd_config_entry* spd_config_find(const spd_drawing* drawing,
                                        const char* key, size_t length) {
  if (drawing == nullptr || key == nullptr) return nullptr;
  size_t lo = 0, hi = drawing->config_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    const spd_config_entry* e = &drawing->config[mid];
    size_t n = length < e->key.length ? length : e->key.length;
    int c = memcmp(key, e->key.data, n);
    if (c == 0) c = (length > e->key.length) - (length < e->key.length);
    if (c == 0) return e;
    if (c < 0)
      hi = mid;
    else
      lo = mid + 1;
  }
  return nullptr;
}
spd_rect spd_tile_rect(const spd_drawing* drawing, const spd_tile* tile) {
  if (drawing == nullptr || tile == nullptr) return (spd_rect){.is_null = true};
  return (spd_rect){.x = (double)tile->tx * 128.0 - drawing->canvas.viewport_x,
                    .y = (double)tile->ty * 128.0 - drawing->canvas.viewport_y,
                    .width = 128,
                    .height = 128};
}
spd_rect spd_painted_bounds(const spd_drawing* drawing,
                            const spd_layer* layer) {
  spd_rect r = {.is_null = true};
  if (drawing == nullptr) return r;
  size_t count = layer == nullptr ? drawing->layer_count : 1;
  uint64_t minx = UINT64_MAX, maxx = 0;
  uint32_t miny = UINT32_MAX, maxy = 0;
  for (size_t i = 0; i < count; i++) {
    const spd_layer* l = layer == nullptr ? &drawing->layers[i] : layer;
    for (size_t j = 0; j < l->tile_count; j++) {
      const spd_tile* t = &l->tiles[j];
      if (t->tx < minx) minx = t->tx;
      if (t->tx > maxx) maxx = t->tx;
      if (t->ty < miny) miny = t->ty;
      if (t->ty > maxy) maxy = t->ty;
      r.is_null = false;
    }
  }
  if (!r.is_null) {
    r.x = (double)minx * 128.0 - drawing->canvas.viewport_x;
    r.y = (double)miny * 128.0 - drawing->canvas.viewport_y;
    r.width = (double)(maxx - minx + 1) * 128.0;
    r.height = (double)((uint64_t)maxy - miny + 1) * 128.0;
  }
  return r;
}
spd_options spd_options_default(void) {
  return (spd_options){.limits = {.max_file_size = 256u * 1024u * 1024u,
                                  .max_layers = 256,
                                  .max_tiles_per_layer = 262144,
                                  .max_tile_bytes = 4u * 1024u * 1024u,
                                  .max_total_tile_bytes = 256u * 1024u * 1024u,
                                  .max_config_entries = 4096,
                                  .max_config_bytes = 16u * 1024u * 1024u,
                                  .max_memory_bytes = 768u * 1024u * 1024u,
                                  .max_work = 1073741824}};
}
