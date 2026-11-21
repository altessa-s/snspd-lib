/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* What the shared library lets out.
 *
 * Everything inside is compiled with hidden visibility, so the plumbing —
 * the arena and the byte reader — does not reach a consumer's
 * symbol table and cannot be linked against by accident. What is marked
 * `SPD_API` below is the contract, and only that. */

#ifndef SNSPD_EXPORT_H
#define SNSPD_EXPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(SNSPD_BUILDING_SHARED)
#define SPD_API __declspec(dllexport)
#elif defined(SNSPD_USING_SHARED)
#define SPD_API __declspec(dllimport)
#else
#define SPD_API
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define SPD_API __attribute__((visibility("default")))
#else
#define SPD_API
#endif

#ifdef __cplusplus
extern "C" {}
#endif

#ifdef __cplusplus
}
#endif
#endif /* SNSPD_EXPORT_H */
