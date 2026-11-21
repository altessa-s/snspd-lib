/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/spd_internal.h"
#include "snspd/snspd.h"
#define CHECK(c)                                      \
  do {                                                \
    if (!(c)) {                                       \
      fprintf(stderr, "line %d: %s\n", __LINE__, #c); \
      exit(1);                                        \
    }                                                 \
  } while (0)
static uint8_t* load(const char* name, size_t* size) {
  char path[1024];
  (void)snprintf(path, sizeof path, "%s/%s.spd", SPD_FIXTURE_DIR, name);
  FILE* f = fopen(path, "rb");
  CHECK(f != nullptr);
  CHECK(fseek(f, 0, SEEK_END) == 0);
  long n = ftell(f);
  CHECK(n >= 0);
  *size = (size_t)n;
  CHECK(fseek(f, 0, SEEK_SET) == 0);
  uint8_t* data = malloc(*size + 1);
  CHECK(data != nullptr);
  CHECK(fread(data, 1, *size, f) == *size);
  CHECK(fclose(f) == 0);
  return data;
}
static spd_drawing* open_fixture(const char* name, const spd_options* options,
                                 spd_status expected) {
  size_t size;
  uint8_t* data = load(name, &size);
  static spd_drawing sentinel;
  spd_drawing* out = &sentinel;
  spd_error error = {.status = SPD_ERR_INVALID_ARGUMENT};
  spd_status rc = spd_open(data, size, options, &out, &error);
  free(data);
  if (rc != expected)
    fprintf(stderr, "fixture %s expected %d got %d (%s)\n", name, expected, rc,
            error.message);
  CHECK(rc == expected);
  CHECK(error.status == rc);
  CHECK(error.message[0] != 0);
  if (rc != SPD_OK) {
    CHECK(out == &sentinel);
    return nullptr;
  }
  return out;
}
static void malformed(void) {
  const char* names[] = {"missing-table",
                         "wrong-types",
                         "negative-tid",
                         "duplicate-layers",
                         "malformed-protobuf",
                         "null-tile",
                         "wrong-tile-type",
                         "view",
                         "bad-config-schema",
                         "bad-decimal",
                         "missing-ls",
                         "bad-group",
                         "overflow-varint",
                         "missing-id",
                         "max-id",
                         "wrong-wire",
                         "bad-key-type",
                         "extra-column",
                         "generated-column",
                         "duplicate-config",
                         "corrupt-page",
                         "virtual-config",
                         "attach-schema",
                         "truncated"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++)
    (void)open_fixture(names[i], nullptr, SPD_ERR_MALFORMED);
  spd_drawing* wal = open_fixture("wal", nullptr, SPD_OK);
  CHECK(wal->had_wal_header && wal->layers[0].tile_count == 3);
  spd_drawing_free(wal);
  (void)open_fixture("corrupt-header", nullptr, SPD_ERR_UNSUPPORTED_FORMAT);
  (void)open_fixture("empty", nullptr, SPD_ERR_UNEXPECTED_EOF);
}
static void extra_tables_and_retained_storage(void) {
  spd_drawing* d = open_fixture("sqlite-prefix", nullptr, SPD_OK);
  CHECK(d->extra_table_count == 4);
  spd_drawing_free(d);
  d = open_fixture("valid", nullptr, SPD_OK);
  size_t retained = sizeof(spd_owner);
  for (spd_block* b = ((spd_owner*)d)->blocks; b != nullptr; b = b->h.next)
    retained += sizeof *b + b->h.size;
  size_t size;
  uint8_t* input = load("valid", &size);
  /* The small model must not retain the much larger SQLite snapshot. */
  CHECK(retained < size);
  free(input);
  CHECK(d->layers[0].tiles[0].png.data[0] == 0x89);
  spd_drawing_free(d);
}
static void model(void) {
  spd_drawing* d = open_fixture("valid", nullptr, SPD_OK);
  CHECK(d->layer_count == 2);
  CHECK(d->extra_table_count == 1);
  CHECK(d->config_count == 18);
  CHECK(!d->unknown_format && d->invalid_tile_count == 0);
  CHECK(d->canvas.width == 1920 && d->canvas.height == 2560 &&
        d->canvas.ppi == 72);
  CHECK(d->canvas.viewport_x == 249984 && d->canvas.viewport_y == 249984);
  CHECK(d->canvas.viewport_scale == 1);
  CHECK(d->canvas.thumbnail.size == 4);
  CHECK(d->layers[0].id == 1 && d->layers[1].id == 9999);
  CHECK(d->layers[1].has_field3 && d->layers[1].field3 == 2);
  CHECK(d->layers[1].has_field4 && d->layers[1].field4 == 1);
  CHECK(d->layers[0].name.length == 7 &&
        memcmp(d->layers[0].name.data, "Layer 1", 7) == 0);
  CHECK(d->layers[0].tile_count == 3 && d->layers[1].tile_count == 0);
  const spd_tile* tiles = d->layers[0].tiles;
  CHECK(tiles[0].tx == 1952 && tiles[0].ty == 1953);
  CHECK(tiles[1].tx == 1952 && tiles[1].ty == 1957);
  CHECK(tiles[2].tx == 1955 && tiles[2].ty == 1954);
  CHECK(tiles[0].tid == 7997345 && tiles[1].tid == 7997349 &&
        tiles[2].tid == 8009634);
  CHECK(tiles[0].png.size > 33 && tiles[0].has_expected_ihdr);
  spd_rect r = spd_tile_rect(d, &tiles[0]);
  CHECK(r.x == -128 && r.y == 0 && r.width == 128 && r.height == 128 &&
        !r.is_null);
  r = spd_tile_rect(d, &tiles[1]);
  CHECK(r.x == -128 && r.y == 512 && r.width == 128 && r.height == 128 &&
        !r.is_null);
  r = spd_tile_rect(d, &tiles[2]);
  CHECK(r.x == 256 && r.y == 128 && r.width == 128 && r.height == 128 &&
        !r.is_null);
  r = spd_painted_bounds(d, nullptr);
  CHECK(r.x == -128 && r.y == 0 && r.width == 512 && r.height == 640 &&
        !r.is_null);
  r = spd_painted_bounds(d, &d->layers[0]);
  CHECK(r.x == -128 && r.y == 0 && r.width == 512 && r.height == 640 &&
        !r.is_null);
  CHECK(spd_painted_bounds(d, &d->layers[1]).is_null);
  const spd_config_entry* e = spd_config_find(d, "unknown.key", 11);
  CHECK(e != nullptr && e->value.size == 2 && e->value.data[1] == 255);
  e = spd_config_find(d, "nul\0key", 7);
  CHECK(e != nullptr && e->value.size == 5);
  e = spd_config_find(d, "null.key", 8);
  CHECK(e != nullptr && e->is_null);
  e = spd_config_find(d, "text.key", 8);
  CHECK(e != nullptr && e->is_text);
  CHECK(spd_config_find(d, "absent", 6) == nullptr);
  CHECK(spd_config_find(nullptr, "", 0) == nullptr);
  CHECK(spd_tile_rect(nullptr, nullptr).is_null);
  CHECK(spd_painted_bounds(nullptr, nullptr).is_null);
  spd_drawing_free(d);
  spd_drawing_free(nullptr);
  d = open_fixture("huge-tid", nullptr, SPD_OK);
  CHECK(d->layers[0].tiles[0].tid == INT64_MAX &&
        d->layers[0].tiles[0].tx == UINT64_C(2251799813685247) &&
        d->layers[0].tiles[0].ty == 4095);
  r = spd_tile_rect(d, &d->layers[0].tiles[0]);
  CHECK(isfinite(r.x) && r.y == 274176 && r.width == 128 && r.height == 128 &&
        !r.is_null);
  r = spd_painted_bounds(d, nullptr);
  CHECK(isfinite(r.x) && r.y == 274176 && r.width == 128 && r.height == 128 &&
        !r.is_null);
  spd_drawing_free(d);
  d = open_fixture("grid-boundary", nullptr, SPD_OK);
  tiles = d->layers[0].tiles;
  CHECK(tiles[0].tid == 0 && tiles[0].tx == 0 && tiles[0].ty == 0);
  CHECK(tiles[1].tid == 4095 && tiles[1].tx == 0 && tiles[1].ty == 4095);
  CHECK(tiles[2].tid == 4096 && tiles[2].tx == 1 && tiles[2].ty == 0);
  r = spd_painted_bounds(d, nullptr);
  CHECK(r.x == -249984 && r.y == -249984 && r.width == 256 &&
        r.height == 524288 && !r.is_null);
  spd_drawing_free(d);
  d = open_fixture("unknown-fields", nullptr, SPD_OK);
  CHECK(d->unknown_layer_field_count == 4 &&
        d->layers[0].unknown_field_count == 3);
  CHECK(d->layers[0].raw.size > 11);
  spd_drawing_free(d);
  d = open_fixture("empty-layers", nullptr, SPD_OK);
  CHECK(d->layer_count == 0);
  spd_drawing_free(d);
  d = open_fixture("null-config", nullptr, SPD_OK);
  CHECK(!d->canvas.has_ppi);
  spd_drawing_free(d);
}
static void policies(void) {
  spd_drawing* d = open_fixture("unknown-version", nullptr, SPD_OK);
  CHECK(d->unknown_format);
  spd_drawing_free(d);
  spd_options o = spd_options_default();
  o.strict = true;
  (void)open_fixture("unknown-version", &o, SPD_ERR_UNSUPPORTED_FORMAT);
  d = open_fixture("valid", &o, SPD_OK);
  spd_drawing_free(d);
  const char* invalid[] = {"non-png", "wrong-ihdr"};
  for (size_t i = 0; i < 2; i++) {
    d = open_fixture(invalid[i], nullptr, SPD_OK);
    CHECK(d->invalid_tile_count == 3);
    spd_drawing_free(d);
    o.require_png_128 = true;
    (void)open_fixture(invalid[i], &o, SPD_ERR_MALFORMED);
  }
  o = spd_options_default();
  o.limits.max_file_size = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_layers = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_tiles_per_layer = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_tile_bytes = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_total_tile_bytes = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_config_entries = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_config_bytes = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_memory_bytes = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_work = 1;
  (void)open_fixture("valid", &o, SPD_ERR_LIMIT);
  o = spd_options_default();
  o.limits.max_memory_bytes = 50000;
  (void)open_fixture("large-config", &o, SPD_ERR_LIMIT);
  /* Exercise failure cleanup at different SQLite/model allocation and VM
   * stages. */
  for (uint64_t budget = 0; budget < 120000; budget += 3000) {
    o = spd_options_default();
    o.limits.max_work = budget;
    size_t n;
    uint8_t* data = load("valid", &n);
    spd_drawing* out = nullptr;
    spd_status rc = spd_open(data, n, &o, &out, nullptr);
    CHECK(rc == SPD_OK || rc == SPD_ERR_LIMIT);
    spd_drawing_free(out);
    free(data);
  }
  for (size_t budget = 0; budget < 180000; budget += 5000) {
    o = spd_options_default();
    o.limits.max_memory_bytes = budget;
    size_t n;
    uint8_t* data = load("valid", &n);
    spd_drawing* out = nullptr;
    spd_status rc = spd_open(data, n, &o, &out, nullptr);
    CHECK(rc == SPD_OK || rc == SPD_ERR_LIMIT);
    spd_drawing_free(out);
    free(data);
  }
}
static int callbacks;
static void sink(spd_log_level level, const char* message, void* context) {
  CHECK(context == &callbacks);
  CHECK(message != nullptr);
  CHECK(level == SPD_LOG_DEBUG || level == SPD_LOG_ERROR);
  callbacks++;
  /* Reentrant parsing is permitted: callbacks run after SQLite shutdown/unlock.
   */
  CHECK(spd_open(nullptr, 0, nullptr, nullptr, nullptr) ==
        SPD_ERR_INVALID_ARGUMENT);
}
static void simple_sink(spd_log_level level, const char* message,
                        void* context) {
  (void)level;
  CHECK(message != nullptr);
  CHECK(context == &callbacks);
  callbacks++;
}
static void reentrant_sink(spd_log_level level, const char* message,
                           void* context) {
  spd_log_silence();
  sink(level, message, context);
}
static void diagnostics(void) {
  spd_drawing* out = nullptr;
  spd_error error;
  CHECK(spd_open(nullptr, 0, nullptr, &out, &error) ==
        SPD_ERR_INVALID_ARGUMENT);
  uint8_t data[100] = {0};
  CHECK(spd_open(data, 100, nullptr, nullptr, nullptr) ==
        SPD_ERR_INVALID_ARGUMENT);
  spd_log_install(simple_sink, &callbacks);
  spd_log_install(simple_sink, &callbacks);
  spd_drawing* d = open_fixture("non-png", nullptr, SPD_OK);
  spd_drawing_free(d);
  CHECK(callbacks == 1);
  spd_log_install(reentrant_sink, &callbacks);
  CHECK(spd_open(nullptr, 0, nullptr, &out, nullptr) ==
        SPD_ERR_INVALID_ARGUMENT);
  CHECK(callbacks == 2);
  spd_log_install(nullptr, nullptr);
  CHECK(strcmp(spd_status_string((spd_status)999), "unknown status") == 0);
}
static void btree_limits(void) {
  const char* names[] = {"dag-config-4", "dag-config-19", "dag-surface_1-4",
                         "dag-surface_1-19"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    (void)open_fixture(names[i], nullptr, SPD_ERR_LIMIT);
    /* With row ceilings removed, the VM work meter must still stop fan-out. */
    spd_options o = spd_options_default();
    o.limits.max_config_entries = SIZE_MAX;
    o.limits.max_tiles_per_layer = SIZE_MAX;
    o.limits.max_work = 200000;
    (void)open_fixture(names[i], &o, SPD_ERR_LIMIT);
  }
  (void)open_fixture("cycle-config", nullptr, SPD_ERR_MALFORMED);
  (void)open_fixture("cycle-surface_1", nullptr, SPD_ERR_MALFORMED);
  (void)open_fixture("cycle-deep-config", nullptr, SPD_ERR_MALFORMED);
  (void)open_fixture("cycle-deep-surface_1", nullptr, SPD_ERR_MALFORMED);
}
static void schema_variants_rounding_and_corruption(void) {
  const char* names[] = {"without-rowid",  "nullable-config",
                         "nonnull-config", "collation-index",
                         "trigger",        "unordered-config"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    spd_drawing* d = open_fixture(names[i], nullptr, SPD_OK);
    CHECK(d->layers[0].tile_count == 3);
    CHECK(spd_config_find(d, "fmt_ver", 7) != nullptr);
    spd_drawing_free(d);
  }
  /* Exercise a decimal-comma process locale when installed. Conversion must
   * use its own C locale; platforms with minimal locale data still run all
   * numerical rounding cases. Restore before other tests. */
  const char* locales[] = {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "fr_FR"};
  for (size_t i = 0; i < sizeof locales / sizeof *locales; i++)
    if (setlocale(LC_NUMERIC, locales[i]) != nullptr) break;
  spd_drawing* d = open_fixture("decimal-rounding", nullptr, SPD_OK);
  CHECK(d->canvas.ppi == 0x1.fffffffffffffp+1023);
  CHECK(d->canvas.width == 0x1p-1022);
  CHECK(d->canvas.viewport_x == 0x0.0000000000001p-1022);
  CHECK(signbit(d->canvas.viewport_y));
  CHECK(d->canvas.viewport_scale == 0x1.999999999999ap-4);
  spd_drawing_free(d);
  d = open_fixture("decimal-tie", nullptr, SPD_OK);
  CHECK(d->canvas.viewport_x == 1.0);
  spd_drawing_free(d);
  d = open_fixture("decimal-above-tie", nullptr, SPD_OK);
  CHECK(d->canvas.viewport_x == 0x1.0000000000001p+0);
  spd_drawing_free(d);
  CHECK(setlocale(LC_NUMERIC, "C") != nullptr);
  size_t size;
  uint8_t* bytes = load("corrupt-page", &size);
  spd_drawing* out = nullptr;
  spd_error error;
  CHECK(spd_open(bytes, size, nullptr, &out, &error) == SPD_ERR_MALFORMED);
  CHECK(error.value != 0);
  free(bytes);
}
int main(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "--btree") == 0) {
    btree_limits();
    puts("fan-out depth 4/19 and cycles: OK");
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--large") == 0) {
    FILE* f = fopen(argv[2], "rb");
    CHECK(f != nullptr && fseek(f, 0, SEEK_END) == 0);
    long length = ftell(f);
    CHECK(length > 0 && fseek(f, 0, SEEK_SET) == 0);
    size_t size = (size_t)length;
    uint8_t* bytes = malloc(size);
    CHECK(bytes != nullptr && fread(bytes, 1, size, f) == size);
    CHECK(fclose(f) == 0);
    spd_drawing* d = nullptr;
    spd_error error;
    spd_status rc = spd_open(bytes, size, nullptr, &d, &error);
    if (rc != SPD_OK)
      fprintf(stderr, "large: %s (%lld)\n", error.message,
              (long long)error.value);
    CHECK(rc == SPD_OK && d->layers[0].tile_count == 63);
    spd_drawing_free(d);
    free(bytes);
    return 0;
  }
  extra_tables_and_retained_storage();
  schema_variants_rounding_and_corruption();
  malformed();
  model();
  policies();
  diagnostics();
  puts("parser, limits, geometry, diagnostics: OK");
  return 0;
}
