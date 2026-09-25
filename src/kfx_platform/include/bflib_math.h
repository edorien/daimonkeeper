/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_math.h
 *     Header file for bflib_math.c.
 * @par Purpose:
 *     Math routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     24 Jan 2009 - 08 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_MATH_H
#define BFLIB_MATH_H

#include "bflib_basics.h"

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Amount of fractional bits in resulting values of trigonometric operations. */
#define LbFPMath_TrigmBits 16

#define CEILING_POS(X) ((X-(int64_t)(X)) > 0 ? (int64_t)(X+1) : (int64_t)(X))
#define CEILING_NEG(X) ((X-(int64_t)(X)) < 0 ? (int64_t)(X-1) : (int64_t)(X))
#define CEILING(X) ( ((X) > 0) ? CEILING_POS(X) : CEILING_NEG(X) )

enum MathOperator {
    MOp_UNDEFINED                      =  0,
    MOp_EQUAL                          =  1,
    MOp_NOT_EQUAL                      =  2,
    MOp_SMALLER                        =  3,
    MOp_GREATER                        =  4,
    MOp_SMALLER_EQ                     =  5,
    MOp_GREATER_EQ                     =  6,
    MOp_LOGIC_AND                      =  7,
    MOp_LOGIC_OR                       =  8,
    MOp_LOGIC_XOR                      =  9,
    MOp_BITWS_AND                      = 10,
    MOp_BITWS_OR                       = 11,
    MOp_BITWS_XOR                      = 12,
    MOp_SUM                            = 13,
    MOp_SUBTRACT                       = 14,
    MOp_MULTIPLY                       = 15,
    MOp_DIVIDE                         = 16,
    MOp_MODULO                         = 17,
};

struct Proportion { // sizeof = 8
    int64_t base_value;
    int64_t distance_ratio;
};

//extern struct Proportion proportions[513];
/******************************************************************************/
#define LB_RANDOM(range,seed) LbRandomSeries(range, seed, __func__, __LINE__)

/******************************************************************************/

int64_t LbSinL(int64_t x);
int64_t LbCosL(int64_t x);
int64_t LbSqrL(int64_t x);
int64_t LbArcTanAngle(int64_t x,int64_t y);
int64_t LbMathOperation(unsigned char opkind, int64_t first_operand, int64_t second_operand);
/** Advance *seed and return a value in [0, range). Range and result are uint32_t (== the 32-bit `unsigned long`
 *  of the Windows build), not `unsigned long`: callers do arithmetic on the result (`RANDOM(11) - 5`,
 *  `(RANDOM(20) - 10) / 2`, ...) and on the range (a negative int converted to unsigned), and those wrap at
 *  32 bits there. With a 64-bit unsigned long the same source gave different numbers, i.e. a different
 *  simulation, on Linux. */
int64_t LbRandomSeries(int64_t range, uint32_t *seed, const char *func_name, uint64_t place);
TbBool LbNumberSignsSame(int64_t num_a, int64_t num_b);
char LbCompareMultiplications(int64_t mul1a, int64_t mul1b, int64_t mul2a, int64_t mul2b);
int64_t LbDiagonalLength(int64_t a, int64_t b);
double LbLerp(double low, double high, double interval);
double LbFmodf(double x, double y);
double lerp_angle(double from, double to, double weight);
double fastPow(double a, double b);

// Moved from engine_camera.h (stage 7 prep, docs/refactor/
// stage-07-kfx-render.md) -- pure geometry/trig, no camera/rendering
// dependency, used pervasively by kfx_sim.
void angles_to_vector(int64_t theta, int64_t phi, int64_t dist, struct ComponentVector *cvect);
int64_t get_angle_xy_to(const struct Coord3d *pos1, const struct Coord3d *pos2);
int64_t get_angle_yz_to(const struct Coord3d *pos1, const struct Coord3d *pos2);
MapCoordDelta get_2d_distance(const struct Coord3d *pos1, const struct Coord3d *pos2);
MapCoordDelta get_2d_distance_squared(const struct Coord3d *pos1, const struct Coord3d *pos2);
int64_t get_angle_xy_to_vec(const struct CoordDelta3d *vec);
int64_t get_angle_yz_to_vec(const struct CoordDelta3d *vec);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
