/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <snspd/snspd.h>

#include <atomic>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
static std::atomic<int> failures;
static void nested(spd_log_level, const char*, void* context) {
  /* Install one immutable binding. Reentry on the same thread must not retain
   * the SQLite lock; all nested calls use an otherwise diagnostic-free file. */
  static thread_local bool in_callback;
  if (in_callback) return;
  in_callback = true;
  auto& data = *static_cast<const std::vector<char>*>(context);
  spd_drawing* d = nullptr;
  if (spd_open(data.data(), data.size(), nullptr, &d, nullptr) != SPD_OK)
    failures++;
  spd_drawing_free(d);
  in_callback = false;
}
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) return 1;
  static std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
  spd_log_install(nested, &data);
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; i++)
    threads.emplace_back([] {
      for (int j = 0; j < 8; j++) {
        /* Both failures occur after acquiring the engine lock, before SQLite
         * starts. The error callback reenters, and other threads keep opening.
         * A file-sized memory budget fits the owner but not owner + snapshot.
         */
        for (int limit = 0; limit < 2; limit++) {
          auto options = spd_options_default();
          if (limit == 0)
            options.limits.max_work = 1;
          else
            options.limits.max_memory_bytes = data.size();
          spd_drawing* rejected = nullptr;
          if (spd_open(data.data(), data.size(), &options, &rejected,
                       nullptr) != SPD_ERR_LIMIT ||
              rejected != nullptr)
            failures++;
          spd_drawing_free(rejected);
        }
        spd_drawing* d = nullptr;
        if (spd_open(data.data(), data.size(), nullptr, &d, nullptr) != SPD_OK)
          failures++;
        if (d != nullptr &&
            (d->layer_count != 2 || d->layers[0].tile_count != 3))
          failures++;
        spd_drawing_free(d);
        if (j % 2 == 0) spd_log_install(nested, &data);
      }
    });
  for (auto& thread : threads) thread.join();
  spd_log_silence();
  return failures == 0 ? 0 : 1;
}
