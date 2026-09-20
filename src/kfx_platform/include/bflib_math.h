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

#define CEILING_POS(X) ((X-(int)(X)) > 0 ? (int)(X+1) : (int)(X))
#define CEILING_NEG(X) ((X-(int)(X)) < 0 ? (int)(X-1) : (int)(X))
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
    long base_value;
    long distance_ratio;
};

//extern struct Proportion proportions[513];
/******************************************************************************/
#define LB_RANDOM(range,seed) LbRandomSeries(range, seed, __func__, __LINE__)

/******************************************************************************/

long LbSinL(long x);
long LbCosL(long x);
long LbSqrL(long x);
int32_t LbArcTanAngle(int32_t x,int32_t y);
long LbMathOperation(unsigned char opkind, long first_operand, long second_operand);
/** Advance *seed and return a value in [0, range). Range and result are uint32_t (== the 32-bit `unsigned long`
 *  of the Windows build), not `unsigned long`: callers do arithmetic on the result (`RANDOM(11) - 5`,
 *  `(RANDOM(20) - 10) / 2`, ...) and on the range (a negative int converted to unsigned), and those wrap at
 *  32 bits there. With a 64-bit unsigned long the same source gave different numbers, i.e. a different
 *  simulation, on Linux. */
uint32_t LbRandomSeries(uint32_t range, uint32_t *seed, const char *func_name, unsigned long place);
TbBool LbNumberSignsSame(long num_a, long num_b);
char LbCompareMultiplications(long mul1a, long mul1b, long mul2a, long mul2b);
long LbDiagonalLength(long a, long b);
float LbLerp(float low, float high, float interval);
float LbFmodf(float x, float y);
float lerp_angle(float from, float to, float weight);
double fastPow(double a, double b);

// Moved from engine_camera.h (stage 7 prep, docs/refactor/
// stage-07-kfx-render.md) -- pure geometry/trig, no camera/rendering
// dependency, used pervasively by kfx_sim.
void angles_to_vector(short theta, short phi, long dist, struct ComponentVector *cvect);
long get_angle_xy_to(const struct Coord3d *pos1, const struct Coord3d *pos2);
long get_angle_yz_to(const struct Coord3d *pos1, const struct Coord3d *pos2);
MapCoordDelta get_2d_distance(const struct Coord3d *pos1, const struct Coord3d *pos2);
MapCoordDelta get_2d_distance_squared(const struct Coord3d *pos1, const struct Coord3d *pos2);
long get_angle_xy_to_vec(const struct CoordDelta3d *vec);
long get_angle_yz_to_vec(const struct CoordDelta3d *vec);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
