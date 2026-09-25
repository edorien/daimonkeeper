#ifndef FRONTGUI_OFFSCREEN_H
#define FRONTGUI_OFFSCREEN_H

// Scoped redirect of the legacy 8bpp raster path at a caller-owned RGBA
// buffer: while an FeOffscreenTarget is alive, legacy LbSprite* /
// panel_map_* / text draw calls land in `buf` instead of the screen, and
// the graphics-window clip covers the whole buffer. If `palette` is
// non-NULL it's forced for the scope (the panel / minimap / cursor
// sprite-decode gotcha -- RendererGetActivePalette() at ImGui present time
// is not the game's engine palette). Everything is restored on
// destruction, in reverse order.
//
// Replaces the hand-written LbScreenStoreGraphicsWindow -> (palette) ->
// RendererSwapFramebufferTarget -> LbScreenSetGraphicsWindow -> ... ->
// restore bracket that six call sites carried (and where forgetting the
// palette restore was a real bug). docs/refactor/ingame-gui/
// 10-maintainability-refactors.md §8.
//
// NOT re-entrant: RendererSwapFramebufferTarget keeps a single static
// save slot, so these must never nest. C++ only.

#ifdef __cplusplus

#include "bflib_video.h" // TbPixel, TbGraphicsWindow, PALETTE_SIZE

class FeOffscreenTarget {
public:
    FeOffscreenTarget(TbPixel *buf, int64_t w, int64_t h, unsigned char *palette = nullptr);
    ~FeOffscreenTarget();
    FeOffscreenTarget(const FeOffscreenTarget &) = delete;
    FeOffscreenTarget &operator=(const FeOffscreenTarget &) = delete;

private:
    TbGraphicsWindow saved_window_;
    TbPixel *saved_target_;
    unsigned char saved_palette_[PALETTE_SIZE];
    bool palette_forced_;
};

#endif // __cplusplus
#endif // FRONTGUI_OFFSCREEN_H
