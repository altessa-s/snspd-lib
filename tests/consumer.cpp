/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <snspd/snspd.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <vector>
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 1;
  std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
  spd_drawing* raw = nullptr;
  if (spd_open(data.data(), data.size(), nullptr, &raw, nullptr) != SPD_OK)
    return 1;
  std::unique_ptr<spd_drawing, decltype(&spd_drawing_free)> owner(
      raw, spd_drawing_free);
  return owner->layer_count == 2 ? 0 : 1;
}
