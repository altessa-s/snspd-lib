/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <stdatomic.h>
#include <stdlib.h>

#include "spd_internal.h"

/* The sink and its context move as one: read separately, a line could reach a
 * new sink with the old sink's context. The pair is published behind a single
 * atomic pointer, and a replacement uses a retained pair rather than editing
 * the live one. */
typedef struct spd_log_binding {
  spd_log_sink sink;
  void* context;
} spd_log_binding;

static _Atomic(spd_log_binding*) spd_log_installed = nullptr;

/* Retain every installed binding so callbacks that already observed one can
 * finish safely. Reinstalling a retained pair needs no new storage. */
typedef struct spd_log_retired {
  struct spd_log_retired* next;
  spd_log_binding binding;
} spd_log_retired;

static _Atomic(spd_log_retired*) spd_log_history = nullptr;

/* Publish callback and context as one immutable binding; allocation failure
 * retains the current sink. */
void spd_log_install(spd_log_sink sink, void* context) {
  if (sink == nullptr) {
    spd_log_silence();
    return;
  }
  /* Acquire the immutable, never-removed entries published by other installers.
   * Concurrent installs of the same new pair may both allocate; that is safe.
   */
  for (spd_log_retired* entry =
           atomic_load_explicit(&spd_log_history, memory_order_acquire);
       entry != nullptr; entry = entry->next) {
    if (entry->binding.sink == sink && entry->binding.context == context) {
      atomic_store_explicit(&spd_log_installed, &entry->binding,
                            memory_order_release);
      return;
    }
  }

  spd_log_retired* entry = malloc(sizeof *entry);
  if (entry == nullptr) return;
  entry->binding = (spd_log_binding){.sink = sink, .context = context};
  spd_log_retired* head =
      atomic_load_explicit(&spd_log_history, memory_order_relaxed);
  do {
    entry->next = head;
  } while (!atomic_compare_exchange_weak_explicit(&spd_log_history, &head,
                                                  entry, memory_order_release,
                                                  memory_order_relaxed));
  atomic_store_explicit(&spd_log_installed, &entry->binding,
                        memory_order_release);
}

/* Publish silence without invalidating bindings already read by parsing
 * threads. */
void spd_log_silence(void) {
  atomic_store_explicit(&spd_log_installed, nullptr, memory_order_release);
}

void spd__internal_log_write(spd_log_level level, const char* message) {
  spd_log_binding* binding =
      atomic_load_explicit(&spd_log_installed, memory_order_acquire);
  if (binding != nullptr) binding->sink(level, message, binding->context);
}
