/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_palettes.h
 *     The game, Freeze and lightning palettes (defined in vidmode_data.cpp).
 * @par Purpose:
 *     Back in kfx_render in refactor pass 4 (S08): kfx_sim names them by enum
 *     ViewPalette (ports/render_port.h). Asset buffers, loaded once and freed at
 *     exit. Included by vidmode.h; on its own by code that only needs these.
 */
/******************************************************************************/
#ifndef DK_GAME_PALETTES_H
#define DK_GAME_PALETTES_H

#ifdef __cplusplus
extern "C" {
#endif
extern unsigned char *engine_palette;
extern unsigned char *blue_palette;
extern unsigned char *lightning_palette;
#ifdef __cplusplus
}
#endif
#endif
