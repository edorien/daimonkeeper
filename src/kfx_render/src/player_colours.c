/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_colours.c
 *     The colours the map, the panels and the power hand draw each player in.
 * @par Purpose:
 *     Presentation only: moved from kfx_sim's player_data.c in refactor pass 4
 *     (S08), as only kfx_render and kfx_frontend read them.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "player_colours.h"

#include "player_data.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/* Per-player colours, one entry per player colour index (red, purple, green,
 * yellow, white, brown, pink, olive, orange).
 *
 * These were palette-index literals, resolved once here against the shipped
 * data/palette.dat (VGA 6-bit, scaled to 8-bit the same way chan6_to_8()
 * does) as part of the true-colour migration -- see
 * docs/refactor/renderer/02a-pixel-format-design.md §4 Bucket B. The original
 * index is kept in a trailing comment on each entry so the mapping stays
 * checkable against the palette file. */
const TbPixel player_path_colours[]  = {
    {133,  44,   0, 255},  /* was idx 131 */
    {137, 113, 149, 255},  /* was idx  90 */
    { 52,  93,   4, 255},  /* was idx 163 */
    {190, 157,   0, 255},  /* was idx 181 */
    {182, 161, 125, 255},  /* was idx  20 */
    { 52,  36,   4, 255},  /* was idx   4 */
    {174,  80, 121, 255},  /* was idx 106 */
    { 76,  60,  20, 255},  /* was idx  52 */
    {190, 109,  52, 255},  /* was idx  42 */
};
const TbPixel player_room_colours[]  = {
    {157,  48,   0, 255},  /* was idx 132 */
    {161, 137, 182, 255},  /* was idx  92 */
    { 56, 113,  12, 255},  /* was idx 164 */
    {230, 214,   0, 255},  /* was idx 183 */
    {190, 170, 133, 255},  /* was idx  21 */
    {157,  48,   0, 255},  /* was idx 132 */
    {202, 105, 165, 255},  /* was idx 108 */
    { 93,  80,  32, 255},  /* was idx  54 */
    {214, 133,  72, 255},  /* was idx  44 */
};
const TbPixel player_flash_colours[] = {
    {186,  48,   0, 255},  /* was idx 133 */
    {190, 170, 222, 255},  /* was idx  94 */
    { 64, 182,  16, 255},  /* was idx 167 */
    {246, 238,  93, 255},  /* was idx 142 */
    {246, 246, 234, 255},  /* was idx  31 */
    {145, 121,  80, 255},  /* was idx  15 */
    {230, 137, 218, 255},  /* was idx 110 */
    { 93,  80,  32, 255},  /* was idx  54 */
    {242, 161,  97, 255},  /* was idx  46 */
};
const TbPixel player_highlight_colours[] = {
    /* all nine were idx 31 -- the palette's near-white */
    {246, 246, 234, 255}, {246, 246, 234, 255}, {246, 246, 234, 255},
    {246, 246, 234, 255}, {246, 246, 234, 255}, {246, 246, 234, 255},
    {246, 246, 234, 255}, {246, 246, 234, 255}, {246, 246, 234, 255},
};
TbPixel get_player_path_colour(int64_t owner)
{
  return player_path_colours[get_player_color_idx(owner % PLAYERS_COUNT)];
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
