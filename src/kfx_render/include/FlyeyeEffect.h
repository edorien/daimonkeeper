/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FlyeyeEffect.h
 *     Flyeye/compound eye lens effect.
 * @par Purpose:
 *     Hexagonal tiling effect for compound eye view.
 * @par Comment:
 *     Resolution-independent implementation using pre-computed lookup table.
 * @author   Tomasz Lis, KeeperFX Team
 * @date     11 Mar 2010 - 09 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_FLYEYEEFFECT_H
#define KFX_FLYEYEEFFECT_H

#include "LensEffect.h"
#include <vector>

/******************************************************************************/

/**
 * Pre-computed flyeye lookup table entry.
 */
struct FlyeyeLookupEntry {
    int64_t src_x;
    int64_t src_y;
};

class FlyeyeEffect : public LensEffect {
public:
    FlyeyeEffect();
    virtual ~FlyeyeEffect();
    
    virtual TbBool Setup(int64_t lens_idx) override;
    virtual void Cleanup() override;
    virtual TbBool Draw(LensRenderContext* ctx) override;
    
private:
    void BuildLookupTable(int64_t width, int64_t height);
    void FreeLookupTable();
    
    int64_t m_current_lens;
    
    // Pre-computed lookup table. m_table_width/height stay separate fields
    // (not just m_lookup_table.size()) since that's what Draw() checks
    // against ctx->width/height to decide whether to rebuild.
    std::vector<FlyeyeLookupEntry> m_lookup_table;
    int64_t m_table_width;
    int64_t m_table_height;
};

/******************************************************************************/
#endif
