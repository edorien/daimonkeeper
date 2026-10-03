/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file port_check.h
 *     Safety checks for the cross-layer callback tables ("ports").
 * @par Purpose:
 *     A callback table (struct *Callbacks / *Predicates) is a struct made
 *     only of function pointers. It has a default table of no-op stubs in
 *     its own library, and main.cpp's wire_ports() installs the real one
 *     at startup. This header provides:
 *     - KFX_ASSERT_PORT_TABLE(T): compile-time check that T holds only
 *       pointer-sized members, so it can be scanned slot by slot;
 *     - kfx_table_complete(): run-time check that no slot of an installed
 *       table is NULL (a member missing from a designated initializer is
 *       silently zero-filled, and C compilers don't warn about it);
 *     - KFX_UNWIRED(table): put in each no-op default stub, so that a call
 *       that lands in a default is logged (once per stub) at the Debug
 *       log level instead of passing unnoticed.
 *     Lives in kfx_platform, the lowest library, because kfx_platform has
 *     callback tables of its own. See docs/refactor-pass2/stage-01-callback-hygiene.md.
 */
/******************************************************************************/
#ifndef KFX_PORT_CHECK_H
#define KFX_PORT_CHECK_H

#include "bflib_basics.h"
#include "globals.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Number of function-pointer slots in callback table type T. */
#define KFX_TABLE_SLOTS(T) (sizeof(T) / sizeof(void (*)(void)))

#ifdef __cplusplus
#define KFX_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define KFX_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/** Place after a callback table's struct definition. */
#define KFX_ASSERT_PORT_TABLE(T) \
    KFX_STATIC_ASSERT(sizeof(T) % sizeof(void (*)(void)) == 0, #T " must hold only function pointers")

/**
 * Checks that none of the `slots` function pointers in `table` is NULL.
 * Logs an error naming the table and the slot index for each NULL slot.
 * @return true if every slot is set.
 */
TbBool kfx_table_complete(const void *table, size_t slots, const char *name);

/** Checks a whole installed table: KFX_TABLE_COMPLETE(ui_port, struct UiPort). */
#define KFX_TABLE_COMPLETE(ptr, T) kfx_table_complete((ptr), KFX_TABLE_SLOTS(T), #T)

/** Logs (at the Debug log level and above), once per stub, that a call reached a default no-op stub of `table`. */
#define KFX_UNWIRED(table) do { \
        static TbBool kfx_unwired_logged_ = false; \
        if (!kfx_unwired_logged_ && KFX_DEBUG_ON(1)) { \
            kfx_unwired_logged_ = true; \
            SYNCDBG(1, "unwired port call: %s default stub reached", table); \
        } \
    } while (0)
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
