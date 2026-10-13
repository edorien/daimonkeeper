/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file fuzz_driver.h
 *     The fuzzing harness interface (security pass S08, fuzz/README.md).
 * @par Purpose:
 *     A target is a function that takes bytes and must neither crash, hang, nor trip a sanitizer, whatever they are.
 *     It is registered with FUZZ_TARGET() and run by fuzz_driver.c, a small coverage-guided mutation fuzzer (gcc has
 *     no libFuzzer): a forked child per input, edge coverage from -fsanitize-coverage=trace-pc, a corpus kept in a
 *     directory.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_FUZZ_DRIVER_H
#define KFX_FUZZ_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FuzzTarget {
    const char *name;
    const char *what;                 /**< one line: what input this is */
    int (*init)(void);                /**< once, before any input and before the first fork; 0 = ok */
    int (*one)(const uint8_t *data, size_t size); /**< one input, in a child process; its return value is ignored */
    size_t max_len;                   /**< longest input worth mutating to (0: 65536) */
    const char *const *dictionary;    /**< tokens to splice in, NULL-terminated, or NULL */
    int (*seeds)(const char *dir);    /**< writes this target's seed inputs into dir (kfx_fuzz <target> seeds <dir>), or NULL */
};

void fuzz_register(const struct FuzzTarget *target);

#ifdef __cplusplus
}
/* registered before main() runs */
#define FUZZ_TARGET(var, ...) \
    static const struct FuzzTarget var = { __VA_ARGS__ }; \
    namespace { struct var##_reg { var##_reg() { fuzz_register(&var); } } var##_reg_instance; }
#else
#define FUZZ_TARGET(var, ...) \
    static const struct FuzzTarget var = { __VA_ARGS__ }; \
    __attribute__((constructor)) static void var##_register(void) { fuzz_register(&var); }
#endif

#endif
