/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
/* A private SQLite instance, including in static archives. Never interpose or
 * configure an application's SQLite. System include suppresses vendor warnings.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define SQLITE_API static __attribute__((unused))
#define SQLITE_EXTERN static
#include <limits.h>
#include <locale.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif
#include <stdio.h>
#include <string.h>

#include <sqlite3.c>

#include "spd_internal.h"

#ifdef _WIN32
static SRWLOCK engine_lock = SRWLOCK_INIT;
static void lock_engine(void) { AcquireSRWLockExclusive(&engine_lock); }
static void unlock_engine(void) { ReleaseSRWLockExclusive(&engine_lock); }
#else
static pthread_mutex_t engine_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock_engine(void) { (void)pthread_mutex_lock(&engine_lock); }
static void unlock_engine(void) { (void)pthread_mutex_unlock(&engine_lock); }
#endif
static spd_meter* engine_meter;

static void* db_malloc(int n) {
  return n > 0 ? spd__internal_alloc(engine_meter, (size_t)n) : nullptr;
}
static void db_free(void* p) { spd__internal_dealloc(engine_meter, p); }
static int db_size(void* p) {
  return p == nullptr ? 0 : (int)(((spd_block*)p - 1)->h.size);
}
static int db_round(int n) { return n; }
static void* db_realloc(void* p, int n) {
  if (p == nullptr) return db_malloc(n);
  if (n <= 0) {
    db_free(p);
    return nullptr;
  }
  size_t old = ((spd_block*)p - 1)->h.size;
  size_t copied = old < (size_t)n ? old : (size_t)n;
  if (!spd__internal_charge(engine_meter, copied)) return nullptr;
  void* q = db_malloc(n);
  if (q != nullptr) {
    memcpy(q, p, copied);
    db_free(p);
  }
  return q;
}
static int db_init(void* context) {
  (void)context;
  return SQLITE_OK;
}
static void db_shutdown(void* context) { (void)context; }

/* No OS VFS is compiled. Even hostile schemas cannot open disk, WAL, temp,
 * journals or extensions. SQLite's :memory: and deserialize use internal files.
 */
static int no_open(sqlite3_vfs* v, const char* path, sqlite3_file* f, int flags,
                   int* out) {
  (void)v;
  (void)path;
  (void)f;
  (void)flags;
  (void)out;
  return SQLITE_CANTOPEN;
}
static int no_delete(sqlite3_vfs* v, const char* p, int sync) {
  (void)v;
  (void)p;
  (void)sync;
  return SQLITE_IOERR_DELETE;
}
static int no_access(sqlite3_vfs* v, const char* p, int flags, int* out) {
  (void)v;
  (void)p;
  (void)flags;
  *out = 0;
  return SQLITE_OK;
}
static int no_path(sqlite3_vfs* v, const char* p, int n, char* out) {
  (void)v;
  (void)p;
  (void)n;
  (void)out;
  return SQLITE_CANTOPEN;
}
static int no_random(sqlite3_vfs* v, int n, char* out) {
  (void)v;
  memset(out, 0, (size_t)n);
  return n;
}
static int no_sleep(sqlite3_vfs* v, int n) {
  (void)v;
  (void)n;
  return 0;
}
static int no_time(sqlite3_vfs* v, double* out) {
  (void)v;
  *out = 2440587.5;
  return SQLITE_OK;
}
static int sqlite3_os_init(void) {
  static sqlite3_vfs vfs = {.iVersion = 1,
                            .szOsFile = sizeof(sqlite3_file),
                            .mxPathname = 1,
                            .zName = "snspd-memory-only",
                            .xOpen = no_open,
                            .xDelete = no_delete,
                            .xAccess = no_access,
                            .xFullPathname = no_path,
                            .xRandomness = no_random,
                            .xSleep = no_sleep,
                            .xCurrentTime = no_time};
  return sqlite3_vfs_register(&vfs, 1);
}
static int sqlite3_os_end(void) { return SQLITE_OK; }

typedef struct parser {
  sqlite3* db;
  spd_owner* owner;
  spd_layer* layer_list;
  spd_meter meter;
  spd_options options;
  size_t config_bytes;
  size_t tile_bytes;
  spd_status status;
  int64_t error_value;
} parser;
static bool fail(parser* p, spd_status status) {
  if (p->status == SPD_OK) p->status = status;
  return false;
}
static bool work(parser* p, uint64_t n) {
  if (spd__internal_charge(&p->meter, n)) return true;
  /* Also stop interruptible engine work between VM progress callbacks. */
  if (p->db != nullptr) sqlite3_interrupt(p->db);
  return false;
}
static int progress(void* context) { return !work(context, 1); }
static bool sql_ok(parser* p, int rc) {
  if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) return true;
  p->error_value = rc;
  return fail(p, rc == SQLITE_NOMEM    ? SPD_ERR_OUT_OF_MEMORY
                 : rc == SQLITE_TOOBIG ? SPD_ERR_LIMIT
                                       : SPD_ERR_MALFORMED);
}
/* Queries below deliberately have no aggregates, ORDER BY, min/max or
 * automatic joins: those can hide unmetered scans/sorts inside one VM opcode.
 * Counts emit rows, and reader-side sorting charges every comparison/swap. */
static sqlite3_stmt* prepare(parser* p, const char* sql) {
  sqlite3_stmt* stmt = nullptr;
  if (!sql_ok(p, sqlite3_prepare_v3(p->db, sql, -1, SQLITE_PREPARE_NO_VTAB,
                                    &stmt, nullptr)))
    return nullptr;
  return stmt;
}
static bool finish(parser* p, sqlite3_stmt* s, int rc) {
  bool ok = sql_ok(p, rc) && rc == SQLITE_DONE;
  int end = sqlite3_finalize(s);
  return sql_ok(p, end) && ok;
}
static void* keep(parser* p, size_t n) {
  return spd__internal_keep(p->owner, &p->meter, n);
}
static spd_bytes copy_bytes(parser* p, const void* data, size_t n) {
  if (n == SIZE_MAX) return (spd_bytes){0};
  uint8_t* out = keep(p, n + 1);
  if (out != nullptr && n != 0) memcpy(out, data, n);
  return (spd_bytes){out, n};
}
static bool bind_name(parser* p, sqlite3_stmt* s, const char* name) {
  return sql_ok(p, sqlite3_bind_text(s, 1, name, -1, SQLITE_STATIC));
}
/* Fixed PRAGMA syntax; only validated integer table names reach snprintf. */
static bool schema(parser* p, const char* table, bool config) {
  sqlite3_stmt* s = prepare(p,
                            "SELECT type,rootpage FROM main.sqlite_schema "
                            "WHERE name=?1 COLLATE BINARY");
  if (s == nullptr) return false;
  if (!bind_name(p, s, table)) {
    sqlite3_finalize(s);
    return false;
  }
  int rc = sqlite3_step(s);
  const char* kind =
      rc == SQLITE_ROW ? (const char*)sqlite3_column_text(s, 0) : nullptr;
  bool valid = rc == SQLITE_ROW && sqlite3_column_type(s, 0) == SQLITE_TEXT &&
               kind != nullptr && strcmp(kind, "table") == 0 &&
               sqlite3_column_int64(s, 1) > 0;
  if (valid) rc = sqlite3_step(s);
  if (!finish(p, s, rc) || !valid) return fail(p, SPD_ERR_MALFORMED);
  char sql[128];
  (void)snprintf(sql, sizeof sql, "PRAGMA main.table_xinfo(\"%s\")", table);
  s = prepare(p, sql);
  if (s == nullptr) return false;
  size_t i = 0;
  const char* names[2] = {config ? "name" : "tid", config ? "value" : "tile"};
  const char* types[2] = {config ? "TEXT" : "INTEGER", "BLOB"};
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    const char* name = (const char*)sqlite3_column_text(s, 1);
    const char* type = (const char*)sqlite3_column_text(s, 2);
    if (!work(p, 1) || i >= 2 || name == nullptr || type == nullptr ||
        strcmp(name, names[i]) != 0 || sqlite3_stricmp(type, types[i]) != 0 ||
        sqlite3_column_int(s, 5) != (i == 0 ? 1 : 0) ||
        sqlite3_column_int(s, 6) != 0 ||
        (!config && sqlite3_column_int(s, 3) != 1)) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_MALFORMED);
    }
    i++;
  }
  return finish(p, s, rc) && (i == 2 || fail(p, SPD_ERR_MALFORMED));
}
static bool count_rows(parser* p, const char* sql, size_t limit,
                       size_t* count) {
  sqlite3_stmt* s = prepare(p, sql);
  if (s == nullptr) return false;
  /* A row-producing scan emits VM steps for every row. OP_Count instead
   * walks an entire b-tree inside one opcode, amplifying shared child pages. */
  size_t n = 0;
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    if (n == limit || !work(p, 1)) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_LIMIT);
    }
    n++;
  }
  *count = n;
  return finish(p, s, rc);
}
typedef int (*entry_order)(parser*, const void*, const void*);
static int config_order(parser* p, const void* a, const void* b) {
  const spd_config_entry *x = a, *y = b;
  size_t n = x->key.length < y->key.length ? x->key.length : y->key.length;
  if (!work(p, n + 1)) return 0;
  int c = memcmp(x->key.data, y->key.data, n);
  return c != 0 ? c
                : (x->key.length > y->key.length) -
                      (x->key.length < y->key.length);
}
static bool sort_entries(parser* p, void* base, size_t count, size_t size,
                         entry_order order) {
  unsigned char* bytes = base;
  /* Build a max heap, then extract the largest entry to the end. */
  for (size_t start = count / 2, end = count; end > 1;) {
    size_t root;
    if (start != 0)
      root = --start;
    else {
      --end;
      if (!work(p, size)) return false;
      for (size_t j = 0; j < size; j++) {
        unsigned char t = bytes[j];
        bytes[j] = bytes[end * size + j];
        bytes[end * size + j] = t;
      }
      root = 0;
    }
    while (root < end / 2) {
      size_t child = root * 2 + 1;
      if (child + 1 < end &&
          order(p, bytes + child * size, bytes + (child + 1) * size) < 0)
        child++;
      if (p->meter.status != SPD_OK) return false;
      if (order(p, bytes + root * size, bytes + child * size) >= 0) break;
      if (!work(p, size)) return false;
      for (size_t j = 0; j < size; j++) {
        unsigned char t = bytes[root * size + j];
        bytes[root * size + j] = bytes[child * size + j];
        bytes[child * size + j] = t;
      }
      root = child;
    }
  }
  return p->meter.status == SPD_OK;
}
static bool configs(parser* p) {
  spd_drawing* d = &p->owner->drawing;
  if (!schema(p, "config", true) ||
      !count_rows(p, "SELECT 1 FROM main.config NOT INDEXED",
                  p->options.limits.max_config_entries, &d->config_count))
    return false;
  if (d->config_count > SIZE_MAX / sizeof(spd_config_entry))
    return fail(p, SPD_ERR_LIMIT);
  spd_config_entry* entries = keep(p, d->config_count * sizeof *entries);
  if (entries == nullptr) return false;
  d->config = entries;
  sqlite3_stmt* s =
      prepare(p, "SELECT name,value FROM main.config NOT INDEXED");
  if (s == nullptr) return false;
  size_t i = 0;
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    int type = sqlite3_column_type(s, 1);
    if (i >= d->config_count || !work(p, 1) ||
        sqlite3_column_type(s, 0) != SQLITE_TEXT ||
        (type != SQLITE_BLOB && type != SQLITE_TEXT && type != SQLITE_NULL)) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_MALFORMED);
    }
    size_t key_size = (size_t)sqlite3_column_bytes(s, 0);
    size_t value_size = (size_t)sqlite3_column_bytes(s, 1);
    size_t ceiling = p->options.limits.max_config_bytes;
    if (key_size > ceiling - p->config_bytes ||
        value_size > ceiling - p->config_bytes - key_size) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_LIMIT);
    }
    p->config_bytes += key_size + value_size;
    spd_bytes key = copy_bytes(p, sqlite3_column_text(s, 0), key_size);
    spd_bytes value = copy_bytes(p, sqlite3_column_blob(s, 1), value_size);
    if (key.data == nullptr || value.data == nullptr) {
      sqlite3_finalize(s);
      return false;
    }
    entries[i++] = (spd_config_entry){.key = {(const char*)key.data, key.size},
                                      .value = value,
                                      .is_null = type == SQLITE_NULL,
                                      .is_text = type == SQLITE_TEXT};
  }
  if (!finish(p, s, rc) || i != d->config_count)
    return fail(p, SPD_ERR_MALFORMED);
  if (!sort_entries(p, entries, i, sizeof *entries, config_order)) return false;
  for (size_t j = 1; j < i; j++)
    if (config_order(p, &entries[j - 1], &entries[j]) == 0)
      return fail(p, SPD_ERR_MALFORMED);
  return p->meter.status == SPD_OK;
}
static const spd_config_entry* config_get(parser* p, const char* key) {
  /* Charge an upper bound on binary lookup comparisons, including long keys. */
  if (!work(p, (strlen(key) + 1) * (sizeof(size_t) * CHAR_BIT))) return nullptr;
  return spd_config_find(&p->owner->drawing, key, strlen(key));
}
static spd_string text_value(parser* p, const char* key) {
  const spd_config_entry* e = config_get(p, key);
  return e == nullptr || e->is_null
             ? (spd_string){0}
             : (spd_string){(const char*)e->value.data, e->value.size};
}
/* Validate bounded ASCII syntax before correctly rounded C-locale conversion.
 * Never change the process locale or accept whitespace, NUL, hex, inf or nan.
 */
static bool decimal(parser* p, spd_string s, double* out) {
  if (s.length == 0 || s.length > 128) return false;
  size_t i = 0, digits = 0;
  if (s.data[i] == '+' || s.data[i] == '-') i++;
  while (i < s.length && s.data[i] >= '0' && s.data[i] <= '9') {
    i++;
    digits++;
  }
  if (i < s.length && s.data[i] == '.') {
    i++;
    while (i < s.length && s.data[i] >= '0' && s.data[i] <= '9') {
      i++;
      digits++;
    }
  }
  if (digits == 0) return false;
  if (i < s.length && (s.data[i] == 'e' || s.data[i] == 'E')) {
    i++;
    if (i < s.length && (s.data[i] == '+' || s.data[i] == '-')) i++;
    size_t start = i;
    while (i < s.length && s.data[i] >= '0' && s.data[i] <= '9') i++;
    if (i == start) return false;
  }
  if (i != s.length) return false;
  char text[129];
  memcpy(text, s.data, s.length);
  text[s.length] = 0;
#ifdef _WIN32
  _locale_t locale = _create_locale(LC_NUMERIC, "C");
#else
  locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
  if (locale == 0) return fail(p, SPD_ERR_OUT_OF_MEMORY);
  char* end;
#ifdef _WIN32
  *out = _strtod_l(text, &end, locale);
  _free_locale(locale);
#else
  *out = strtod_l(text, &end, locale);
  freelocale(locale);
#endif
  return end == text + s.length && isfinite(*out);
}
static bool number(parser* p, const char* key, double* out, bool* present,
                   bool positive) {
  spd_string s = text_value(p, key);
  *present = s.data != nullptr;
  if (!*present) return true;
  if (!work(p, s.length) || !decimal(p, s, out) || (positive && *out <= 0))
    return fail(p, SPD_ERR_MALFORMED);
  return true;
}
static bool canvas(parser* p) {
  spd_canvas* c = &p->owner->drawing.canvas;
  c->fmt_ver = text_value(p, "fmt_ver");
  c->paper_mode = text_value(p, "paperMode");
  c->template_name = text_value(p, "template_name");
  c->template_info = text_value(p, "templateInfo");
  c->app_version = text_value(p, "appVersion");
  const spd_config_entry* thumb = config_get(p, "thumbnail");
  const spd_config_entry* templ = config_get(p, "templateData");
  if (thumb != nullptr && !thumb->is_null) c->thumbnail = thumb->value;
  if (templ != nullptr && !templ->is_null) c->template_data = templ->value;
  p->owner->drawing.unknown_format =
      c->fmt_ver.length != 1 || c->fmt_ver.data[0] != '2';
  if (p->options.strict && p->owner->drawing.unknown_format)
    return fail(p, SPD_ERR_UNSUPPORTED_FORMAT);
  return number(p, "surface.width", &c->width, &c->has_width, true) &&
         number(p, "surface.height", &c->height, &c->has_height, true) &&
         number(p, "ppi", &c->ppi, &c->has_ppi, true) &&
         number(p, "vp.x", &c->viewport_x, &c->has_viewport_x, false) &&
         number(p, "vp.y", &c->viewport_y, &c->has_viewport_y, false) &&
         number(p, "vp.scale", &c->viewport_scale, &c->has_viewport_scale,
                true);
}

typedef struct proto {
  const uint8_t* bytes;
  size_t size, pos;
} proto;
static bool varint(parser* p, proto* r, uint64_t* out) {
  *out = 0;
  for (unsigned i = 0; i < 10; i++) {
    if (r->pos == r->size || !work(p, 1)) return fail(p, SPD_ERR_MALFORMED);
    uint8_t b = r->bytes[r->pos++];
    if (i == 9 && b > 1) return fail(p, SPD_ERR_MALFORMED);
    *out |= (uint64_t)(b & 127u) << (i * 7);
    if ((b & 128u) == 0) return true;
  }
  return fail(p, SPD_ERR_MALFORMED);
}
static bool delimited(parser* p, proto* r, proto* child) {
  uint64_t n;
  if (!varint(p, r, &n) || n > r->size - r->pos)
    return fail(p, SPD_ERR_MALFORMED);
  *child = (proto){r->bytes + r->pos, (size_t)n, 0};
  r->pos += (size_t)n;
  return true;
}
/* Unknown protobuf groups are skipped with an explicit nesting limit. */
static bool skip(parser* p, proto* r, uint64_t tag, unsigned depth) {
  uint64_t ignored;
  proto child;
  size_t n;
  switch (tag & 7u) {
    case 0:
      return varint(p, r, &ignored);
    case 1:
      n = 8;
      break;
    case 2:
      if (!delimited(p, r, &child)) return false;
      return work(p, child.size);
    case 3:
      if (depth >= 32) return fail(p, SPD_ERR_LIMIT);
      while (r->pos < r->size) {
        uint64_t nested;
        if (!varint(p, r, &nested) || nested >> 3 == 0 ||
            nested >> 3 > 536870911)
          return fail(p, SPD_ERR_MALFORMED);
        if ((nested & 7u) == 4)
          return (nested >> 3 == tag >> 3) || fail(p, SPD_ERR_MALFORMED);
        if (!skip(p, r, nested, depth + 1)) return false;
      }
      return fail(p, SPD_ERR_MALFORMED);
    case 5:
      n = 4;
      break;
    default:
      return fail(p, SPD_ERR_MALFORMED);
  }
  if (n > r->size - r->pos) return fail(p, SPD_ERR_MALFORMED);
  r->pos += n;
  return work(p, n);
}
static bool layer_message(parser* p, proto r, spd_layer* l) {
  bool has_id = false, has_name = false;
  l->raw = (spd_bytes){r.bytes, r.size};
  while (r.pos < r.size) {
    uint64_t tag, value;
    if (!varint(p, &r, &tag) || tag >> 3 == 0 || tag >> 3 > 536870911)
      return fail(p, SPD_ERR_MALFORMED);
    unsigned field = (unsigned)(tag >> 3);
    if ((field == 1 || field == 3 || field == 4) && (tag & 7u) == 0) {
      if (!varint(p, &r, &value)) return false;
      if (field == 1) {
        l->id = value;
        has_id = true;
      }
      if (field == 3) {
        l->field3 = value;
        l->has_field3 = true;
      }
      if (field == 4) {
        l->field4 = value;
        l->has_field4 = true;
      }
    } else if (field == 2 && (tag & 7u) == 2) {
      proto name;
      if (!delimited(p, &r, &name)) return false;
      spd_bytes b = copy_bytes(p, name.bytes, name.size);
      if (b.data == nullptr) return false;
      l->name = (spd_string){(const char*)b.data, b.size};
      has_name = true;
    } else {
      if (field <= 4) return fail(p, SPD_ERR_MALFORMED);
      if (!skip(p, &r, tag, 0)) return false;
      l->unknown_field_count++;
    }
  }
  return (has_id && has_name && l->id <= INT64_MAX) ||
         fail(p, SPD_ERR_MALFORMED);
}
static bool layers(parser* p) {
  const spd_config_entry* e = config_get(p, "ls");
  if (e == nullptr || e->is_null) return fail(p, SPD_ERR_MALFORMED);
  proto r = {e->value.data, e->value.size, 0};
  size_t count = 0;
  for (unsigned pass = 0; pass < 2; pass++) {
    r.pos = 0;
    size_t index = 0;
    while (r.pos < r.size) {
      uint64_t tag;
      if (!varint(p, &r, &tag) || tag >> 3 == 0 || tag >> 3 > 536870911)
        return fail(p, SPD_ERR_MALFORMED);
      if (tag == 10) {
        proto child;
        if (!delimited(p, &r, &child)) return false;
        if (pass == 0) {
          if (count == p->options.limits.max_layers)
            return fail(p, SPD_ERR_LIMIT);
          count++;
        } else {
          spd_layer* l = &p->layer_list[index];
          if (!layer_message(p, child, l)) return false;
          for (size_t j = 0; j < index; j++) {
            if (!work(p, 1)) return false;
            if (l->id == p->owner->drawing.layers[j].id)
              return fail(p, SPD_ERR_MALFORMED);
          }
          p->owner->drawing.unknown_layer_field_count += l->unknown_field_count;
          index++;
        }
      } else {
        if (tag >> 3 == 1 || !skip(p, &r, tag, 0))
          return fail(p, SPD_ERR_MALFORMED);
        if (pass == 1) p->owner->drawing.unknown_layer_field_count++;
      }
    }
    if (pass == 0) {
      if (count > SIZE_MAX / sizeof(spd_layer)) return fail(p, SPD_ERR_LIMIT);
      p->layer_list = keep(p, count * sizeof(spd_layer));
      p->owner->drawing.layers = p->layer_list;
      if (p->owner->drawing.layers == nullptr) return false;
      p->owner->drawing.layer_count = count;
    }
  }
  return true;
}
static bool expected_png(spd_bytes b) {
  static const uint8_t start[16] = {137, 80, 78, 71, 13,  10,  26,  10,
                                    0,   0,  0,  13, 'I', 'H', 'D', 'R'};
  static const uint8_t dimensions[8] = {0, 0, 0, 128, 0, 0, 0, 128};
  return b.size >= 33 && memcmp(b.data, start, sizeof start) == 0 &&
         memcmp(b.data + 16, dimensions, sizeof dimensions) == 0;
}
static int tile_order(parser* p, const void* a, const void* b) {
  const spd_tile *x = a, *y = b;
  if (!work(p, 1)) return 0;
  return (x->tid > y->tid) - (x->tid < y->tid);
}
static bool tiles(parser* p, spd_layer* l) {
  char table[64], sql[160];
  (void)snprintf(table, sizeof table, "surface_%llu",
                 (unsigned long long)l->id);
  if (!schema(p, table, false)) return false;
  (void)snprintf(sql, sizeof sql, "SELECT 1 FROM main.\"%s\" NOT INDEXED",
                 table);
  if (!count_rows(p, sql, p->options.limits.max_tiles_per_layer,
                  &l->tile_count))
    return false;
  if (l->tile_count > SIZE_MAX / sizeof(spd_tile))
    return fail(p, SPD_ERR_LIMIT);
  spd_tile* list = keep(p, l->tile_count * sizeof *list);
  if (list == nullptr) return false;
  l->tiles = list;
  (void)snprintf(sql, sizeof sql,
                 "SELECT tid,tile FROM main.\"%s\" NOT INDEXED", table);
  sqlite3_stmt* s = prepare(p, sql);
  if (s == nullptr) return false;
  int rc;
  size_t i = 0;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    int64_t tid = sqlite3_column_int64(s, 0);
    if (!work(p, 1) || i >= l->tile_count ||
        sqlite3_column_type(s, 0) != SQLITE_INTEGER ||
        sqlite3_column_type(s, 1) != SQLITE_BLOB || tid < 0) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_MALFORMED);
    }
    size_t n = (size_t)sqlite3_column_bytes(s, 1);
    if (n > p->options.limits.max_tile_bytes ||
        n > p->options.limits.max_total_tile_bytes - p->tile_bytes) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_LIMIT);
    }
    p->tile_bytes += n;
    spd_bytes b = copy_bytes(p, sqlite3_column_blob(s, 1), n);
    if (b.data == nullptr) {
      sqlite3_finalize(s);
      return false;
    }
    bool png = expected_png(b);
    if (!png && p->options.require_png_128) {
      sqlite3_finalize(s);
      return fail(p, SPD_ERR_MALFORMED);
    }
    if (!png) p->owner->drawing.invalid_tile_count++;
    list[i++] = (spd_tile){.tid = tid,
                           .tx = (uint64_t)tid / 4096,
                           .ty = (uint32_t)((uint64_t)tid % 4096),
                           .png = b,
                           .has_expected_ihdr = png};
  }
  if (!finish(p, s, rc) || i != l->tile_count)
    return fail(p, SPD_ERR_MALFORMED);
  if (!sort_entries(p, list, i, sizeof *list, tile_order)) return false;
  for (size_t j = 1; j < i; j++) {
    if (!work(p, 1)) return false;
    if (list[j - 1].tid == list[j].tid) return fail(p, SPD_ERR_MALFORMED);
  }
  return true;
}
static bool extras(parser* p) {
  size_t count;
  if (!count_rows(
          p,
          "SELECT 1 FROM main.sqlite_schema WHERE "
          "type='table' AND substr(name,1,7) <> 'sqlite_' COLLATE NOCASE",
          SIZE_MAX, &count))
    return false;
  size_t known = p->owner->drawing.layer_count + 1;
  if (count < known) return fail(p, SPD_ERR_MALFORMED);
  p->owner->drawing.extra_table_count = count - known;
  return true;
}
static bool setup(parser* p, uint8_t* input, size_t size) {
  sqlite3_mem_methods methods = {.xMalloc = db_malloc,
                                 .xFree = db_free,
                                 .xRealloc = db_realloc,
                                 .xSize = db_size,
                                 .xRoundup = db_round,
                                 .xInit = db_init,
                                 .xShutdown = db_shutdown};
  if (!sql_ok(p, sqlite3_config(SQLITE_CONFIG_MALLOC, &methods)) ||
      !sql_ok(p, sqlite3_initialize()) ||
      !sql_ok(p, sqlite3_open_v2(":memory:", &p->db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                     SQLITE_OPEN_NOMUTEX,
                                 nullptr)))
    return false;
  sqlite3_progress_handler(p->db, 1, progress, p);
  sqlite3_limit(p->db, SQLITE_LIMIT_LENGTH, 64 * 1024 * 1024);
  sqlite3_limit(p->db, SQLITE_LIMIT_SQL_LENGTH, 4096);
  sqlite3_limit(p->db, SQLITE_LIMIT_COLUMN, 32);
  sqlite3_limit(p->db, SQLITE_LIMIT_EXPR_DEPTH, 32);
  sqlite3_limit(p->db, SQLITE_LIMIT_COMPOUND_SELECT, 1);
  sqlite3_limit(p->db, SQLITE_LIMIT_VDBE_OP, 10000);
  sqlite3_limit(p->db, SQLITE_LIMIT_FUNCTION_ARG, 8);
  sqlite3_limit(p->db, SQLITE_LIMIT_ATTACHED, 0);
  sqlite3_limit(p->db, SQLITE_LIMIT_VARIABLE_NUMBER, 8);
  sqlite3_limit(p->db, SQLITE_LIMIT_TRIGGER_DEPTH, 0);
  sqlite3_limit(p->db, SQLITE_LIMIT_WORKER_THREADS, 0);
  if (!sql_ok(
          p, sqlite3_db_config(p->db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr)) ||
      !sql_ok(p, sqlite3_db_config(p->db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0,
                                   nullptr)) ||
      !sql_ok(p, sqlite3_db_config(p->db, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0,
                                   nullptr)) ||
      !sql_ok(p, sqlite3_db_config(p->db, SQLITE_DBCONFIG_ENABLE_VIEW, 0,
                                   nullptr)) ||
      !sql_ok(p, sqlite3_db_config(p->db, SQLITE_DBCONFIG_ENABLE_FKEY, 0,
                                   nullptr)) ||
      !sql_ok(p, sqlite3_db_config(p->db, SQLITE_DBCONFIG_LOOKASIDE, nullptr, 0,
                                   0)) ||
      !sql_ok(p, sqlite3_deserialize(p->db, "main", input, (sqlite3_int64)size,
                                     (sqlite3_int64)size,
                                     SQLITE_DESERIALIZE_READONLY)))
    return false;
  return sql_ok(
      p, sqlite3_exec(p->db,
                      "PRAGMA query_only=ON; PRAGMA temp_store=MEMORY; PRAGMA "
                      "cache_size=-1024; PRAGMA cell_size_check=ON;",
                      nullptr, nullptr, nullptr));
}
spd_status spd_open(const void* data, size_t size, const spd_options* options,
                    spd_drawing** output, spd_error* error) {
  parser p = {.options = options == nullptr ? spd_options_default() : *options};
  p.meter.ceiling = p.options.limits.max_memory_bytes;
  p.meter.work = p.options.limits.max_work;
  if (data == nullptr || output == nullptr)
    p.status = SPD_ERR_INVALID_ARGUMENT;
  else if (size > p.options.limits.max_file_size || size > INT64_MAX)
    p.status = SPD_ERR_LIMIT;
  else if (size < 16)
    p.status = SPD_ERR_UNEXPECTED_EOF;
  else if (memcmp(data, "SQLite format 3\0", 16) != 0)
    p.status = SPD_ERR_UNSUPPORTED_FORMAT;
  else if (size < 100)
    p.status = SPD_ERR_UNEXPECTED_EOF;
  else if ((((const uint8_t*)data)[18] != 1 &&
            ((const uint8_t*)data)[18] != 2) ||
           (((const uint8_t*)data)[19] != 1 && ((const uint8_t*)data)[19] != 2))
    p.status = SPD_ERR_UNSUPPORTED_FORMAT;
  if (p.status == SPD_OK) {
    p.owner = spd__internal_alloc(&p.meter, sizeof *p.owner);
    if (p.owner != nullptr) {
      memset(p.owner, 0, sizeof *p.owner);
      /* Wait before allocating the snapshot so queued opens do not each
       * retain a full input copy. */
      lock_engine();
      uint8_t* input =
          work(&p, size) ? spd__internal_alloc(&p.meter, size) : nullptr;
      if (input != nullptr) {
        memcpy(input, data, size);
        p.owner->drawing.had_wal_header = input[18] == 2 || input[19] == 2;
        /* SQLite documents this normalization for deserialize. The supplied
         * bytes must already be a self-contained snapshot; no sidecar is read.
         */
        input[18] = input[19] = 1;
        engine_meter = &p.meter;
        bool ok =
            setup(&p, input, size) && configs(&p) && canvas(&p) && layers(&p);
        for (size_t i = 0; ok && i < p.owner->drawing.layer_count; i++)
          ok = tiles(&p, &p.layer_list[i]);
        if (ok) ok = extras(&p);
        (void)sqlite3_close(p.db);
        spd__internal_dealloc(&p.meter, input);
        (void)sqlite3_shutdown();
        engine_meter = nullptr;
        if (!ok && p.status == SPD_OK && p.meter.status == SPD_OK)
          p.status = SPD_ERR_MALFORMED;
      }
      unlock_engine();
    }
  }
  if (p.meter.status != SPD_OK) p.status = p.meter.status;
  if (p.status == SPD_OK && p.owner == nullptr)
    p.status = SPD_ERR_OUT_OF_MEMORY;
  if (p.status == SPD_OK)
    *output = &p.owner->drawing;
  else if (p.owner != nullptr)
    spd_drawing_free(&p.owner->drawing);
  if (error != nullptr) {
    *error = (spd_error){.status = p.status, .value = p.error_value};
    (void)snprintf(error->message, sizeof error->message, "%s",
                   spd_status_string(p.status));
  }
  if (p.status != SPD_OK)
    spd__internal_log_write(SPD_LOG_ERROR, spd_status_string(p.status));
  else if (p.owner->drawing.unknown_format ||
           p.owner->drawing.invalid_tile_count ||
           p.owner->drawing.unknown_layer_field_count ||
           p.owner->drawing.extra_table_count)
    spd__internal_log_write(SPD_LOG_DEBUG,
                            "uninterpreted format details retained");
  return p.status;
}
