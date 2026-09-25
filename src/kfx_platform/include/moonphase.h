#ifndef MOONPHASE_H
#define MOONPHASE_H

#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

    extern int64_t is_full_moon;
    extern int64_t is_near_full_moon;
    extern int64_t is_new_moon;
    extern int64_t is_near_new_moon;
    
    int64_t calculate_moon_phase(int64_t do_calculate, int64_t add_to_log);

#ifdef __cplusplus
}
#endif

#endif // MOONPHASE_H
