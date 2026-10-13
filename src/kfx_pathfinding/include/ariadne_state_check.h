/** @file ariadne_state_check.h
 *     The range macros the navigation mesh's saved-state checks share (security pass S06).
 */
#ifndef DK_ARIADNE_STATE_CHECK_H
#define DK_ARIADNE_STATE_CHECK_H

#include "state_check.h"

/** `value` of the named element is within lo..hi; otherwise note it and (unless surveying) leave with 0. */
#define ARI_RANGE(c, value, lo, hi, what, i) do { \
        const int64_t v_ = (int64_t)(value); \
        if ((v_ < (int64_t)(lo)) || (v_ > (int64_t)(hi))) { \
            if (!state_check_note((c), (what), (long long)(i), v_, "%s[%lld] = %lld is outside %lld..%lld", (what), (long long)(i), \
                (long long)v_, (long long)(lo), (long long)(hi))) \
                return 0; \
        } \
    } while (0)

#endif
