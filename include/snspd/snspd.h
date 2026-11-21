/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef SNSPD_H
#define SNSPD_H
#include "snspd/log.h"
#include "snspd/parser.h"
#include "snspd/types.h"
#define SNSPD_VERSION_MAJOR 1
#define SNSPD_VERSION_MINOR 0
#define SNSPD_VERSION_PATCH 0
#define SNSPD_STRINGIFY_(x) #x
#define SNSPD_STRINGIFY(x) SNSPD_STRINGIFY_(x)
#define SNSPD_VERSION_STRING                                    \
  SNSPD_STRINGIFY(SNSPD_VERSION_MAJOR)                          \
  "." SNSPD_STRINGIFY(SNSPD_VERSION_MINOR) "." SNSPD_STRINGIFY( \
      SNSPD_VERSION_PATCH)
#endif
