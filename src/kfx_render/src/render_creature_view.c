/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file render_creature_view.c
 *     Drawing the first-person possession view.
 * @par Purpose:
 *     draw_creature_view() (the view, through the eye lens when one is
 *     active) and the attack swipe overlay, moved from kfx_sim's
 *     thing_creature.c (docs/refactor-pass2/stage-06-presentation-out-of-sim.md):
 *     their only caller is engine_redraw.c, and they only draw.
 * @par Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "render_creature_view.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_sprite.h"
#include "bflib_video.h"
#include "bflib_vidraw.h"
#include "creature_control.h"
#include "creature_instances.h"
#include "engine_redraw.h"
#include "engine_render.h"
#include "kfx_sim_state.h"
#include "lens_api.h"
#include "local_camera.h"
#include "player_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/**
 * Randomise the draw direction of the swipe sprite in the first-person possession view.
 *
 * Sets local_state.swipe_sprite_drawLR to either TRUE or FALSE.
 *
 * Draw direction is either: left-to-right (TRUE) or right-to-left (FALSE)
 */
static void randomise_swipe_graphic_direction(void)
{
    local_state.swipe_sprite_drawLR = UNSYNC_RANDOM(2); // equal chance to be left-to-right or right-to-left
}

static void draw_swipe_graphic_impl(void);

/* gpu-v2: the swipe is a translucent overlay drawn over the finished scene; with the Vulkan
 * renderer it must be recorded onto the GPU image (see RendererOverlayBegin) rather than blended
 * against the transparent CPU layer, which turned the whole view into flat colour. */
void draw_swipe_graphic(void)
{
    RendererOverlayBegin();
    draw_swipe_graphic_impl();
    RendererOverlayEnd();
}

static void draw_swipe_graphic_impl(void)
{
    struct PlayerInfo* myplyr = get_my_player();
    struct Thing* thing = thing_get(myplyr->controlled_thing_idx);
    if (thing_is_creature(thing))
    {
        struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
        if (instance_draws_possession_swipe(cctrl->instance_id))
        {
            RendererSetDrawFlags(Lb_SPRITE_TRANSPAR4);
            int64_t n = (int64_t)cctrl->inst_turn * (5 << 8) / cctrl->inst_total_turns;
            int64_t allwidth = 0;
            int64_t i = max(((llabs(n) >> 8) -1),0);
            if (i >= SWIPE_SPRITE_FRAMES)
                i = SWIPE_SPRITE_FRAMES-1;
            const struct TbSprite* sprlist = get_sprite(swipe_sprites, SWIPE_SPRITES_X * SWIPE_SPRITES_Y * i);
            if (sprlist == NULL)
            {
                ERRORLOG("Failed to draw swipe sprite for thing %" PRId64, (int64_t)thing->index);
                return;
            }
            const struct TbSprite* startspr = &sprlist[1];
            const struct TbSprite* endspr = &sprlist[1];
            for (n=0; n < SWIPE_SPRITES_X; n++)
            {
                allwidth += endspr->SWidth;
                endspr++;
            }
            int64_t units_per_px = (LbScreenWidth() * 59 / 64) * 16 / allwidth;
            int64_t scrpos_y = (MyScreenHeight * 16 / units_per_px - (startspr->SHeight + endspr->SHeight)) / 2;
            const struct TbSprite *spr;
            int64_t scrpos_x;
            if (local_state.swipe_sprite_drawLR)
            {
                int64_t delta_y = sprlist[1].SHeight;
                for (i=0; i < SWIPE_SPRITES_X*SWIPE_SPRITES_Y; i+=SWIPE_SPRITES_X)
                {
                    spr = &startspr[i];
                    scrpos_x = ((MyScreenWidth + (2 * local_state.engine_window_x)) * 16 / units_per_px - allwidth)/ 2;
                    for (n=0; n < SWIPE_SPRITES_X; n++)
                    {
                        LbSpriteDrawResized(scrpos_x * units_per_px / 16, scrpos_y * units_per_px / 16, units_per_px, spr);
                        scrpos_x += spr->SWidth;
                        spr++;
                    }
                    scrpos_y += delta_y;
                }
            } else
            {
                RendererSetDrawFlags(Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_FLIP_HORIZ);
                for (i=0; i < SWIPE_SPRITES_X*SWIPE_SPRITES_Y; i+=SWIPE_SPRITES_X)
                {
                    spr = &sprlist[SWIPE_SPRITES_X+i];
                    int64_t delta_y = spr->SHeight;
                    scrpos_x = (MyScreenWidth * 16 / units_per_px - allwidth) / 2;
                    for (n=0; n < SWIPE_SPRITES_X; n++)
                    {
                        LbSpriteDrawResized(scrpos_x * units_per_px / 16, scrpos_y * units_per_px / 16, units_per_px, spr);
                        scrpos_x += spr->SWidth;
                        spr--;
                    }
                    scrpos_y += delta_y;
                }
            }
            RendererSetDrawFlags(0);
            return;
        }
    }
    // we get here many times a second when in possession mode and not attacking: to randomise the swipe direction
    randomise_swipe_graphic_direction();
}

void draw_creature_view(struct Thing *thing)
{
  // If no eye lens required - just draw on the screen, directly
  struct PlayerInfo* player = get_my_player();
  struct Camera* render_cam = get_local_camera(&player->cameras[CamIV_FirstPerson]);
  if (!lens_is_ready())
  {
      engine(player, render_cam);
      // Still need to draw swipe even when no lens effect is active.
      draw_swipe_graphic();
      return;
  }
  // GPU world renderer (gpu-v2 Phase C.3): keep the scene on the GPU rather
  // than redirecting the whole engine into a CPU buffer. Render normally,
  // read the finished frame (GPU layer + CPU overlays) back into the lens
  // source buffer, then run the unchanged CPU lens post-pass over it. The
  // per-effect decision is 'CPU post-pass over a read-back frame' -- see
  // docs/refactor/renderer/gpu-v2/07-phased-delivery.md.
  if (RendererWorldFrameActive())
  {
      TbPixel* srcmem = lens_get_render_target();
      uint64_t src_width = lens_get_render_target_width();
      engine(player, render_cam);
      draw_swipe_graphic();
      int64_t vw = local_state.engine_window_width / pixel_size;
      int64_t vh = local_state.engine_window_height / pixel_size;
      int64_t vx = local_state.engine_window_x / pixel_size;
      int64_t vy = local_state.engine_window_y / pixel_size;
      memset(srcmem, 0, src_width * lens_get_render_target_height() * sizeof(TbPixel));
      RendererCopyFrameRect(srcmem, src_width, vx, vy, vw, vh);
      setup_engine_window(0, 0, MyScreenWidth, MyScreenHeight);
      draw_lens_effect(RendererGetFramebuffer() + vy * lbDisplay.GraphicsScreenWidth + vx,
          lbDisplay.GraphicsScreenWidth, srcmem, src_width, vw, vh, vx, kfx_sim_state.applied_lens_type);
      return;
  }
  // So there is an eye lens - we have to put a buffer in place of screen,
  // draw on that buffer, an then copy it to screen applying lens effect.
  TbPixel* scrmem = lens_get_render_target();
  uint64_t render_width = lens_get_render_target_width();
  uint64_t render_height = lens_get_render_target_height();
  
  // Store previous graphics settings
  TbGraphicsWindow grwnd;
  LbScreenStoreGraphicsWindow(&grwnd);
  // Prepare new settings
  memset(scrmem, 0, render_width*render_height*sizeof(TbPixel));
  TbPixel* wscr_cp = RendererSwapFramebufferTarget(scrmem, render_width, render_height);
  LbScreenSetGraphicsWindow(0, 0, MyScreenWidth/pixel_size, MyScreenHeight/pixel_size);
  // Draw on our buffer
  setup_engine_window(0, 0, MyScreenWidth, MyScreenHeight);
  engine(player, render_cam);
  // Draw swipe into buffer BEFORE lens effects (so overlay renders on top of swipe)
  draw_swipe_graphic();
  // Get the actual viewport dimensions (accounts for sidebar)
  int64_t view_width = local_state.engine_window_width / pixel_size;
  int64_t view_height = local_state.engine_window_height / pixel_size;
  int64_t view_x = local_state.engine_window_x / pixel_size;
  int64_t view_y = local_state.engine_window_y / pixel_size;
  // Restore original graphics settings
  RendererRestoreFramebufferTarget(wscr_cp);
  LbScreenLoadGraphicsWindow(&grwnd);
  // Draw the buffer on real screen using actual viewport dimensions
  setup_engine_window(0, 0, MyScreenWidth, MyScreenHeight);
  // Apply lens effect to the viewport area only (not including sidebar)
  // Pass full srcbuf so displacement map lookups work correctly
  // Calculate 2D viewport offset for destination buffer
  int64_t dst_offset = view_y * lbDisplay.GraphicsScreenWidth + view_x;
  draw_lens_effect(RendererGetFramebuffer() + dst_offset, lbDisplay.GraphicsScreenWidth,
      scrmem, render_width, view_width, view_height, view_x, kfx_sim_state.applied_lens_type);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
