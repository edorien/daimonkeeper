/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file landview_image.h
 *     docs/refactor/editor/fx-plans/08 §11.3 (K7a) -- the land-view background as an indexed image: from `<name>.png`
 *     (an 8-bit indexed PNG as it is, any other PNG quantised to 256 colours) or from `<name>.raw` + `<name>.pal`.
 * @par Comment:
 *     Shared by the game (load_map_and_window) and the Campaign editor's land-view page. The image is always
 *     1280 x 960; the palette is the game's 6-bit VGA one (0..63 per channel, 768 bytes).
 */
/******************************************************************************/
#ifndef DK_LANDVIEW_IMAGE_H
#define DK_LANDVIEW_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define LANDVIEW_WIDTH 1280
#define LANDVIEW_HEIGHT 960
#define LANDVIEW_PIXELS (LANDVIEW_WIDTH * LANDVIEW_HEIGHT)
#define LANDVIEW_PALETTE_SIZE 768

#ifdef __cplusplus
extern "C" {
#endif

/** C entry for the game: loads `<base>.png` if it exists, else `<base>.raw` + `<base>.pal`, into `pixels`
 *  (LANDVIEW_PIXELS bytes) and `palette` (LANDVIEW_PALETTE_SIZE bytes). `base` is the full path without extension.
 *  Returns 1 on success, 0 when there is no PNG (the caller then uses its own raw loader) and -1 on a PNG that cannot
 *  be used (logged: wrong size, unreadable). */
int landview_load_png_indexed(const char *base, uint8_t *pixels, uint8_t *palette);

#ifdef __cplusplus
}

#include <string>
#include <vector>

struct LandviewImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // width * height palette indices
    uint8_t palette[LANDVIEW_PALETTE_SIZE] = {0}; // 6-bit VGA
};

/** Decodes a PNG (`len` bytes). 8-bit indexed images keep their pixels and palette; anything else is quantised to 256
 *  colours. Fails (with `error`) on a damaged file or when the size is not 1280 x 960. */
bool landview_decode_png(const uint8_t *data, size_t len, LandviewImage &out, std::string &error);

/** Median-cut quantisation of an RGBA image (alpha ignored) to 256 colours, deterministic. */
void landview_quantise(const uint8_t *rgba, int width, int height, LandviewImage &out);

/** Loads `<base>.png`, else `<base>.raw` + `<base>.pal`. `base`: full path without extension. `used_png` (may be null) says which. */
bool landview_load(const std::string &base, LandviewImage &out, std::string &error, bool *used_png = nullptr);

/** Expands to RGBA (8-bit channels) for display. */
std::vector<uint8_t> landview_to_rgba(const LandviewImage &img);

/** Loads `<base>.png` only (no `.raw` fallback); false when it is missing or not usable. */
bool landview_png_exists(const std::string &base);
#endif

#endif
