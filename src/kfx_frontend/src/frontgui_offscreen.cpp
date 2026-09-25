#include "pre_inc.h"
#include "frontgui_offscreen.h"
#include "renderer/RendererManager.h" // RendererSwapFramebufferTarget / Restore, RendererPaletteGet / Set
#include "post_inc.h"

#include <cstdint>

FeOffscreenTarget::FeOffscreenTarget(TbPixel *buf, int64_t w, int64_t h, unsigned char *palette)
    : saved_target_(nullptr), palette_forced_(palette != nullptr)
{
    if (palette_forced_)
    {
        RendererPaletteGet(saved_palette_);
        RendererPaletteSet(palette);
    }
    LbScreenStoreGraphicsWindow(&saved_window_);
    saved_target_ = RendererSwapFramebufferTarget(buf, (uint64_t)w, (uint64_t)h);
    LbScreenSetGraphicsWindow(0, 0, w, h);
}

FeOffscreenTarget::~FeOffscreenTarget()
{
    RendererRestoreFramebufferTarget(saved_target_);
    LbScreenLoadGraphicsWindow(&saved_window_);
    if (palette_forced_)
        RendererPaletteSet(saved_palette_);
}
