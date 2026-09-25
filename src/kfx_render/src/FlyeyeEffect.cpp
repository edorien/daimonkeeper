/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FlyeyeEffect.cpp
 *     Flyeye/compound eye lens effect implementation.
 * @par Purpose:
 *     Self-contained fish eye/hexagonal tiling effect.
 * @par Comment:
 *     Creates a compound eye view by rendering hexagonal tiles with radial distortion.
 *     Resolution-independent via proper hex polygon rasterization in reference space.
 * @author   Tomasz Lis, KeeperFX Team
 * @date     11 Mar 2010 - 09 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "FlyeyeEffect.h"

#include <cmath>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "config_lenses.h"
#include "lens_api.h"

#include "post_inc.h"

/******************************************************************************/

// Reference dimensions - effect computed at 640x480, then scaled
static const int64_t REF_WIDTH = 640;
static const int64_t REF_HEIGHT = 480;
static const int64_t REF_MAXSIZE = 640;

// Maximum strips per scanline
#define MAX_STRIPS_PER_LINE 26

/**
 * Strip entry for scanline - defines start X and source offset.
 */
struct FlyeyeStrip {
    int64_t start_x;
    int64_t source_off_x;
    int64_t source_off_y;
};

/**
 * Scanline data for reference-space rasterization.
 */
struct FlyeyeScanline {
    int64_t num_strips;
    FlyeyeStrip strips[MAX_STRIPS_PER_LINE];
};

// Temporary storage for reference-space scanlines
static FlyeyeScanline* g_ref_scanlines = nullptr;

/******************************************************************************/

/**
 * Add a strip to a scanline, maintaining sorted order by start_x.
 * Direct port of CHex::AddScan from original.
 */
static void AddScan(FlyeyeScanline* scan, int64_t strip_len, int64_t len_limit, 
                    int64_t source_off_x, int64_t source_off_y)
{
    if (strip_len < 0) strip_len = 0;
    if (strip_len >= REF_WIDTH || strip_len >= len_limit) return;
    if (scan->num_strips >= MAX_STRIPS_PER_LINE) return;
    
    // Find insertion point (sorted by start_x)
    int64_t insert_idx = 0;
    for (int64_t i = 0; i < scan->num_strips; i++)
    {
        int64_t cur_len = scan->strips[i].start_x;
        if (strip_len == cur_len)
        {
            // Update existing
            scan->strips[i].source_off_x = source_off_x;
            scan->strips[i].source_off_y = source_off_y;
            return;
        }
        if (cur_len > strip_len)
            break;
        insert_idx++;
    }
    
    // Shift to make room
    for (int64_t i = scan->num_strips; i > insert_idx; i--)
    {
        scan->strips[i] = scan->strips[i - 1];
    }
    
    // Insert
    scan->strips[insert_idx].start_x = strip_len;
    scan->strips[insert_idx].source_off_x = source_off_x;
    scan->strips[insert_idx].source_off_y = source_off_y;
    scan->num_strips++;
}

/**
 * Rasterize one hexagon into reference-space scanlines.
 * Direct port of CHex class from original lens_flyeye.cpp.
 */
static void RasterizeHexRef(int64_t hex_x, int64_t hex_y)
{
    // Compute hex center in reference coordinate system
    int64_t mwidth = 50 * hex_x;
    int64_t mheight = 60 * hex_y;
    if ((hex_x & 1) != 0)
        mheight += 30;
    
    // Distortion parameters for reference resolution
    double ldpar1 = REF_MAXSIZE * 0.0175;
    double ldpar2 = REF_MAXSIZE * 0.0025;
    
    // 6 hex vertices (relative to screen center)
    int64_t arrA[6], arrB[6];
    arrA[0] = mwidth - 35;  arrB[0] = mheight + 30;
    arrA[1] = mwidth - 15;  arrB[1] = mheight;
    arrA[2] = mwidth + 15;  arrB[2] = mheight;
    arrA[3] = mwidth + 35;  arrB[3] = mheight + 30;
    arrA[4] = mwidth + 15;  arrB[4] = mheight + 60;
    arrA[5] = mwidth - 15;  arrB[5] = mheight + 60;
    
    // Apply radial distortion to vertices
    double ref_center_x = REF_WIDTH * 0.5;
    double ref_center_y = REF_HEIGHT * 0.5;
    
    for (int64_t i = 0; i < 6; i++)
    {
        double varA = arrA[i];
        double varB = arrB[i];
        double len = sqrt(varA * varA + varB * varB) * 0.0025 + 1.0;
        arrA[i] = (int64_t)(ref_center_x + (varA / len) * ldpar2);
        arrB[i] = (int64_t)(ref_center_y + (varB / len) * ldpar2);
    }
    
    // Source offset for this hex
    int64_t source_strip_w = (int64_t)(-hex_x * ldpar1);
    int64_t source_strip_h = (int64_t)(-hex_y * ldpar1);
    
    // Find topmost vertex
    int64_t min_idx = 0;
    for (int64_t i = 1; i < 6; i++)
    {
        if (arrB[i] < arrB[min_idx])
            min_idx = i;
    }
    
    // Rasterize using original BlitHex algorithm
    int64_t scan_num = arrB[min_idx];
    int64_t first_idx = (min_idx + 1) % 6;
    int64_t last_idx = (min_idx + 5) % 6;
    int64_t deltaV1 = 0, deltaV2 = 0;
    int64_t posV1 = 0, posV2 = 0;
    int64_t counter1 = 0, counter2 = 0;
    
    while (1)
    {
        // Advance left edge (counterclockwise)
        int64_t i = first_idx;
        while (counter1 == 0)
        {
            first_idx = (first_idx + 5) % 6;
            if (first_idx == i)
                return;
            posV1 = arrA[first_idx] << 16;
            int64_t n = (first_idx + 5) % 6;
            counter1 = arrB[n] - arrB[first_idx];
            if (counter1 > 0)
            {
                deltaV1 = ((arrA[n] << 16) - posV1) / counter1;
            }
        }
        
        // Advance right edge (clockwise)
        i = last_idx;
        while (counter2 == 0)
        {
            last_idx = (last_idx + 1) % 6;
            if (last_idx == i)
                return;
            int64_t n = (last_idx + 1) % 6;
            counter2 = arrB[n] - arrB[last_idx];
            posV2 = arrA[last_idx] << 16;
            if (counter2 > 0)
            {
                deltaV2 = ((arrA[n] << 16) - posV2) / counter2;
            }
        }
        
        if (counter1 <= 0 || counter2 <= 0)
            return;
        
        // Add strip to scanline
        if (scan_num >= 0 && scan_num < REF_HEIGHT)
        {
            AddScan(&g_ref_scanlines[scan_num], posV1 >> 16, posV2 >> 16,
                    source_strip_w, source_strip_h);
        }
        
        counter1--;
        counter2--;
        posV1 += deltaV1;
        posV2 += deltaV2;
        scan_num++;
    }
}

/******************************************************************************/

FlyeyeEffect::FlyeyeEffect()
    : LensEffect(LensEffectType::Flyeye, "Flyeye")
    , m_current_lens(-1)
    , m_table_width(0)
    , m_table_height(0)
{
}

FlyeyeEffect::~FlyeyeEffect()
{
    Cleanup();
}

void FlyeyeEffect::FreeLookupTable()
{
    m_lookup_table.clear();
    m_lookup_table.shrink_to_fit();
    m_table_width = 0;
    m_table_height = 0;
}

/**
 * Build lookup table:
 * 1. Rasterize hexes in reference 640x480 space
 * 2. Convert reference scanlines to screen-resolution lookup table
 */
void FlyeyeEffect::BuildLookupTable(int64_t width, int64_t height)
{
    FreeLookupTable();

    // Allocate reference scanlines
    g_ref_scanlines = (FlyeyeScanline*)malloc(REF_HEIGHT * sizeof(FlyeyeScanline));
    if (g_ref_scanlines == nullptr)
    {
        ERRORLOG("Failed to allocate flyeye reference scanlines");
        return;
    }

    // Initialize reference scanlines
    for (int64_t y = 0; y < REF_HEIGHT; y++)
    {
        g_ref_scanlines[y].num_strips = 0;
    }

    // Rasterize all hexagons in reference space
    for (int64_t hex_y = -12; hex_y <= 12; hex_y++)
    {
        for (int64_t hex_x = -12; hex_x <= 12; hex_x++)
        {
            RasterizeHexRef(hex_x, hex_y);
        }
    }

    // Allocate lookup table for actual resolution. vector::resize throws
    // std::bad_alloc on failure rather than returning null like malloc()
    // did; catch it to keep this function's original "log and gracefully
    // skip" contract.
    try {
        m_lookup_table.resize((size_t)width * (size_t)height);
    } catch (const std::bad_alloc &) {
        size_t table_size = (size_t)width * (size_t)height * sizeof(FlyeyeLookupEntry);
        ERRORLOG("Failed to allocate flyeye lookup table (%" PRIuSIZE " bytes)", (uint64_t)(SZCAST(table_size)));
        free(g_ref_scanlines);
        g_ref_scanlines = nullptr;
        return;
    }

    m_table_width = width;
    m_table_height = height;

    // Scale factors
    double scale_x = (double)width / REF_WIDTH;
    double scale_y = (double)height / REF_HEIGHT;

    // Convert reference scanlines to screen-resolution lookup table
    FlyeyeLookupEntry* entry = m_lookup_table.data();
    
    for (int64_t y = 0; y < height; y++)
    {
        // Map screen Y to reference Y
        int64_t ref_y = (int64_t)(y / scale_y);
        if (ref_y >= REF_HEIGHT) ref_y = REF_HEIGHT - 1;
        
        FlyeyeScanline* scan = &g_ref_scanlines[ref_y];
        
        for (int64_t x = 0; x < width; x++)
        {
            // Map screen X to reference X
            int64_t ref_x = (int64_t)(x / scale_x);
            if (ref_x >= REF_WIDTH) ref_x = REF_WIDTH - 1;
            
            // Find which strip this reference X falls into
            int64_t cur_off_x = 0;
            int64_t cur_off_y = 0;
            
            for (int64_t i = 0; i < scan->num_strips; i++)
            {
                if (ref_x >= scan->strips[i].start_x)
                {
                    cur_off_x = scan->strips[i].source_off_x;
                    cur_off_y = scan->strips[i].source_off_y;
                }
                else
                {
                    break;
                }
            }
            
            // Compute source in reference space, then scale to screen
            int64_t ref_src_x = ref_x + cur_off_x;
            int64_t ref_src_y = ref_y + cur_off_y;
            
            // Clamp reference coords
            if (ref_src_x < 0) ref_src_x = 0;
            if (ref_src_x >= REF_WIDTH) ref_src_x = REF_WIDTH - 1;
            if (ref_src_y < 0) ref_src_y = 0;
            if (ref_src_y >= REF_HEIGHT) ref_src_y = REF_HEIGHT - 1;
            
            // Scale to actual screen coordinates
            int64_t src_x = (int64_t)(ref_src_x * scale_x);
            int64_t src_y = (int64_t)(ref_src_y * scale_y);
            
            if (src_x >= width) src_x = width - 1;
            if (src_y >= height) src_y = height - 1;
            
            entry->src_x = (int64_t)src_x;
            entry->src_y = (int64_t)src_y;
            entry++;
        }
    }
    
    free(g_ref_scanlines);
    g_ref_scanlines = nullptr;
    
    SYNCDBG(7, "Built flyeye lookup table %" PRId64 "x%" PRId64, (int64_t)(width), (int64_t)(height));
}

TbBool FlyeyeEffect::Setup(int64_t lens_idx)
{
    SYNCDBG(8, "Setting up flyeye effect for lens %" PRId64, (int64_t)(lens_idx));
    
    FreeLookupTable();
    m_current_lens = lens_idx;
    
    SYNCDBG(7, "Flyeye effect ready");
    return true;
}

void FlyeyeEffect::Cleanup()
{
    FreeLookupTable();
    m_current_lens = -1;
}

TbBool FlyeyeEffect::Draw(LensRenderContext* ctx)
{
    if (m_current_lens < 0)
    {
        return false;
    }
    
    // Build lookup table if needed
    if (m_lookup_table.empty() ||
        m_table_width != ctx->width ||
        m_table_height != ctx->height)
    {
        BuildLookupTable(ctx->width, ctx->height);
        if (m_lookup_table.empty())
        {
            return false;
        }
    }

    // Fast lookup-based rendering
    TbPixel* viewport_src = ctx->srcbuf + ctx->viewport_x;
    TbPixel* dst = ctx->dstbuf;
    FlyeyeLookupEntry* entry = m_lookup_table.data();
    
    for (int64_t y = 0; y < ctx->height; y++)
    {
        for (int64_t x = 0; x < ctx->width; x++)
        {
            dst[x] = viewport_src[entry->src_y * ctx->srcpitch + entry->src_x];
            entry++;
        }
        dst += ctx->dstpitch;
    }
    
    ctx->buffer_copied = true;
    return true;
}