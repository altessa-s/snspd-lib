/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snspd/snspd.h"

/* Validate Unicode scalar encodings, rejecting overlongs and surrogates. */
static bool utf8(spd_string s) {
  for (size_t i = 0; i < s.length;) {
    unsigned char c = (unsigned char)s.data[i++];
    if (c < 128) continue;
    size_t n;
    unsigned char lo = 0x80, hi = 0xbf;
    if (c >= 0xc2 && c <= 0xdf)
      n = 1;
    else if (c >= 0xe0 && c <= 0xef) {
      n = 2;
      if (c == 0xe0) lo = 0xa0;
      if (c == 0xed) hi = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
      n = 3;
      if (c == 0xf0) lo = 0x90;
      if (c == 0xf4) hi = 0x8f;
    } else
      return false;
    if (n > s.length - i) return false;
    unsigned char first = (unsigned char)s.data[i];
    if (first < lo || first > hi) return false;
    for (size_t j = 1; j < n; j++) {
      unsigned char next = (unsigned char)s.data[i + j];
      if (next < 0x80 || next > 0xbf) return false;
    }
    i += n;
  }
  return true;
}
static void string(spd_string s) {
  if (!utf8(s)) {
    printf("{\"hex\":\"");
    for (size_t i = 0; i < s.length; i++)
      printf("%02x", (unsigned)(unsigned char)s.data[i]);
    printf("\"}");
    return;
  }
  putchar('"');
  for (size_t i = 0; i < s.length; i++) {
    unsigned char c = (unsigned char)s.data[i];
    if (c == '"' || c == '\\') {
      putchar('\\');
      putchar(c);
    } else if (c < 32 || c == 127)
      printf("\\u%04x", (unsigned)c);
    else
      putchar(c);
  }
  putchar('"');
}
static void key(const char* s) { string((spd_string){s, strlen(s)}); }
static bool opaque(spd_string s) {
  const char* names[] = {"ls", "thumbnail", "templateData"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++)
    if (s.length == strlen(names[i]) && memcmp(s.data, names[i], s.length) == 0)
      return true;
  return false;
}
static bool printable(spd_bytes b) {
  for (size_t i = 0; i < b.size; i++)
    if (b.data[i] < 32 || b.data[i] == 127) return false;
  return utf8((spd_string){(const char*)b.data, b.size});
}
static void json(const spd_drawing* d, size_t size) {
  printf(
      "{\"had_wal_header\":%s,\"file_bytes\":%zu,\"config_count\":%zu,\"extra_"
      "table_count\":%zu,\"unknown_format\":%s,\"invalid_tile_count\":%zu,"
      "\"unknown_layer_field_count\":%zu,\"config\":[",
      d->had_wal_header ? "true" : "false", size, d->config_count,
      d->extra_table_count, d->unknown_format ? "true" : "false",
      d->invalid_tile_count, d->unknown_layer_field_count);
  for (size_t i = 0; i < d->config_count; i++) {
    const spd_config_entry* e = &d->config[i];
    if (i != 0) putchar(',');
    printf("{\"key\":");
    string(e->key);
    printf(",\"bytes\":%zu,\"storage\":", e->value.size);
    key(e->is_null ? "null" : e->is_text ? "text" : "blob");
    if (!e->is_null && !opaque(e->key) && printable(e->value)) {
      printf(",\"value\":");
      string((spd_string){(const char*)e->value.data, e->value.size});
    }
    putchar('}');
  }
  printf("],\"layer_count\":%zu,\"layers\":[", d->layer_count);
  for (size_t i = 0; i < d->layer_count; i++) {
    const spd_layer* l = &d->layers[i];
    if (i != 0) putchar(',');
    printf("{\"id\":%llu,\"name\":", (unsigned long long)l->id);
    string(l->name);
    printf(",\"tile_count\":%zu,\"protobuf_bytes\":%zu,\"field3\":",
           l->tile_count, l->raw.size);
    if (l->has_field3)
      printf("%llu", (unsigned long long)l->field3);
    else
      printf("null");
    printf(",\"field4\":");
    if (l->has_field4)
      printf("%llu", (unsigned long long)l->field4);
    else
      printf("null");
    printf(",\"unknown_field_count\":%zu,\"grid\":", l->unknown_field_count);
    uint64_t minx = UINT64_MAX, maxx = 0;
    uint32_t miny = UINT32_MAX, maxy = 0;
    size_t bytes = 0, minbytes = SIZE_MAX, maxbytes = 0;
    for (size_t j = 0; j < l->tile_count; j++) {
      const spd_tile* t = &l->tiles[j];
      if (t->tx < minx) minx = t->tx;
      if (t->tx > maxx) maxx = t->tx;
      if (t->ty < miny) miny = t->ty;
      if (t->ty > maxy) maxy = t->ty;
      bytes += t->png.size;
      if (t->png.size < minbytes) minbytes = t->png.size;
      if (t->png.size > maxbytes) maxbytes = t->png.size;
    }
    if (l->tile_count != 0)
      printf("{\"min_tx\":%llu,\"max_tx\":%llu,\"min_ty\":%u,\"max_ty\":%u}",
             (unsigned long long)minx, (unsigned long long)maxx, miny, maxy);
    else
      printf("null");
    printf(",\"tile_bytes\":%zu,\"min_tile_bytes\":%zu,\"max_tile_bytes\":%zu}",
           bytes, l->tile_count == 0 ? 0 : minbytes, maxbytes);
  }
  printf("]}\n");
}
static void text(const spd_drawing* d, size_t size) {
  printf(
      "had_wal_header %d\nfile_bytes %zu\nconfig_count %zu\nextra_table_count "
      "%zu\nunknown_format %d\ninvalid_tile_count "
      "%zu\nunknown_layer_field_count %zu\n",
      d->had_wal_header, size, d->config_count, d->extra_table_count,
      d->unknown_format, d->invalid_tile_count, d->unknown_layer_field_count);
  for (size_t i = 0; i < d->config_count; i++) {
    const spd_config_entry* e = &d->config[i];
    printf("config ");
    string(e->key);
    printf(" bytes %zu storage %s", e->value.size,
           e->is_null   ? "null"
           : e->is_text ? "text"
                        : "blob");
    if (!e->is_null && !opaque(e->key) && printable(e->value)) {
      printf(" value ");
      string((spd_string){(const char*)e->value.data, e->value.size});
    }
    putchar('\n');
  }
  printf("layer_count %zu\n", d->layer_count);
  for (size_t i = 0; i < d->layer_count; i++) {
    const spd_layer* l = &d->layers[i];
    printf("layer id %llu name ", (unsigned long long)l->id);
    string(l->name);
    printf(" tiles %zu protobuf_bytes %zu\n", l->tile_count, l->raw.size);
    if (l->has_field3) printf("field3 %llu\n", (unsigned long long)l->field3);
    if (l->has_field4) printf("field4 %llu\n", (unsigned long long)l->field4);
    printf("unknown_field_count %zu\n", l->unknown_field_count);
    spd_rect r = spd_painted_bounds(d, l);
    if (!r.is_null)
      printf("bounds_pixels %.17g %.17g %.17g %.17g\n", r.x, r.y, r.width,
             r.height);
    size_t bytes = 0;
    for (size_t j = 0; j < l->tile_count; j++) bytes += l->tiles[j].png.size;
    printf("tile_bytes %zu\n", bytes);
    if (l->tile_count != 0) {
      uint64_t minx = UINT64_MAX, maxx = 0;
      uint32_t miny = UINT32_MAX, maxy = 0;
      size_t minbytes = SIZE_MAX, maxbytes = 0;
      for (size_t j = 0; j < l->tile_count; j++) {
        const spd_tile* t = &l->tiles[j];
        if (t->tx < minx) minx = t->tx;
        if (t->tx > maxx) maxx = t->tx;
        if (t->ty < miny) miny = t->ty;
        if (t->ty > maxy) maxy = t->ty;
        if (t->png.size < minbytes) minbytes = t->png.size;
        if (t->png.size > maxbytes) maxbytes = t->png.size;
      }
      printf("grid_tx %llu %llu\ngrid_ty %u %u\ntile_size_range %zu %zu\n",
             (unsigned long long)minx, (unsigned long long)maxx, miny, maxy,
             minbytes, maxbytes);
    } else
      printf("grid null\n");
  }
}
int main(int argc, char** argv) {
  const char* path = nullptr;
  const char* thumb = nullptr;
  bool as_json = false;
  spd_options options = spd_options_default();
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--json") == 0)
      as_json = true;
    else if (strcmp(argv[i], "--strict") == 0)
      options.strict = true;
    else if (strcmp(argv[i], "--require-png-128") == 0)
      options.require_png_128 = true;
    else if (strcmp(argv[i], "--thumbnail") == 0 && i + 1 < argc)
      thumb = argv[++i];
    else if (argv[i][0] != '-' && path == nullptr)
      path = argv[i];
    else {
      fprintf(stderr, "invalid option or extra argument\n");
      return 2;
    }
  }
  if (path == nullptr) {
    fprintf(stderr,
            "usage: spddump [--json] [--strict] [--require-png-128] "
            "[--thumbnail output.jpg] file.spd\n");
    return 2;
  }
  FILE* f = fopen(path, "rb");
  if (f == nullptr) {
    fprintf(stderr, "cannot open input\n");
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 1;
  }
  long length = ftell(f);
  if (length < 0 || (uint64_t)length > options.limits.max_file_size ||
      fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    fprintf(stderr, "input size limit or seek failure\n");
    return 1;
  }
  size_t size = (size_t)length;
  uint8_t* data = malloc(size == 0 ? 1 : size);
  if (data == nullptr) {
    fclose(f);
    return 1;
  }
  bool read_ok = fread(data, 1, size, f) == size;
  if (fclose(f) != 0) read_ok = false;
  if (!read_ok) {
    free(data);
    return 1;
  }
  spd_drawing* d = nullptr;
  spd_error error;
  spd_status status = spd_open(data, size, &options, &d, &error);
  free(data);
  if (status != SPD_OK) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  bool ok = true;
  if (thumb != nullptr) {
    /* Exclusive creation avoids truncating the input or any existing file. */
    f = fopen(thumb, "wbx");
    if (f == nullptr)
      ok = false;
    else {
      ok = fwrite(d->canvas.thumbnail.data, 1, d->canvas.thumbnail.size, f) ==
           d->canvas.thumbnail.size;
      if (fclose(f) != 0) ok = false;
    }
    if (!ok) fprintf(stderr, "cannot create thumbnail output\n");
  }
  if (as_json)
    json(d, size);
  else
    text(d, size);
  if (fflush(stdout) != 0 || ferror(stdout)) ok = false;
  spd_drawing_free(d);
  return ok ? 0 : 1;
}
