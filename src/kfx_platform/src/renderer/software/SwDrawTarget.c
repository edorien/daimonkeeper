/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SwDrawTarget.c
 *     Where the software raster draws.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/software/SwDrawTarget.h"
#include "bflib_vidraw.h"   /* vec_screen, poly_screen, vec_map, vec_screen_width, vec_window_* */
#include "post_inc.h"

/******************************************************************************/

TbPixel* SwTargetWScreen(void)           { return lbDisplay.WScreen; }
TbPixel* SwTargetGraphicsWindowPtr(void) { return lbDisplay.GraphicsWindowPtr; }
int64_t SwTargetScanline(void)              { return (int64_t)lbDisplay.GraphicsScreenWidth; }
int64_t SwTargetScreenHeight(void)          { return (int64_t)lbDisplay.GraphicsScreenHeight; }

int64_t SwTargetWindowX(void)      { return (int64_t)lbDisplay.GraphicsWindowX; }
int64_t SwTargetWindowY(void)      { return (int64_t)lbDisplay.GraphicsWindowY; }
int64_t SwTargetWindowWidth(void)  { return (int64_t)lbDisplay.GraphicsWindowWidth; }
int64_t SwTargetWindowHeight(void) { return (int64_t)lbDisplay.GraphicsWindowHeight; }

TbPixel* SwTargetVecScreen(void)        { return vec_screen; }
TbPixel* SwTargetPolyScreen(void)       { return poly_screen; }
const unsigned char* SwTargetVecMap(void) { return vec_map; }
uint64_t SwTargetVecScreenWidth(void) { return vec_screen_width; }
int64_t SwTargetVecWindowWidth(void)       { return vec_window_width; }
int64_t SwTargetVecWindowHeight(void)      { return vec_window_height; }
