/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file landview_image.cpp
 *     See landview_image.h.
 */
#include "pre_inc.h"
#include "landview_image.h"

#include <spng.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "globals.h"
#include "bflib_dernc.h"
#include "post_inc.h"

/******************************************************************************/
namespace {

bool slurp_file(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

struct Bin
{
    int r, g, b; // 5-bit channels
    uint64_t count;
};

struct Box
{
    size_t first, last; // range in the bin array
    uint64_t count;
};

int channel(const Bin &b, int c)
{
    return c == 0 ? b.r : c == 1 ? b.g : b.b;
}

} // namespace

/******************************************************************************/
void landview_quantise(const uint8_t *rgba, int width, int height, LandviewImage &out)
{
    // Histogram over 5 bits per channel, then median cut over the bins (weighted by pixel count).
    std::vector<uint64_t> hist(32768, 0);
    const size_t n = (size_t)width * (size_t)height;
    for (size_t i = 0; i < n; i++)
        hist[((size_t)(rgba[i * 4] >> 3) << 10) | ((size_t)(rgba[i * 4 + 1] >> 3) << 5) | (size_t)(rgba[i * 4 + 2] >> 3)]++;
    std::vector<Bin> bins;
    for (size_t k = 0; k < hist.size(); k++)
        if (hist[k] != 0)
            bins.push_back({(int)(k >> 10), (int)((k >> 5) & 31), (int)(k & 31), hist[k]});

    std::vector<Box> boxes;
    boxes.push_back({0, bins.size(), (uint64_t)n});
    while (boxes.size() < 256)
    {
        // The box with the widest channel range that still has more than one bin (ties: lowest index).
        size_t best = boxes.size();
        int best_range = 0, best_channel = 0;
        for (size_t i = 0; i < boxes.size(); i++)
        {
            if (boxes[i].last - boxes[i].first < 2)
                continue;
            for (int c = 0; c < 3; c++)
            {
                int lo = 31, hi = 0;
                for (size_t k = boxes[i].first; k < boxes[i].last; k++)
                {
                    lo = std::min(lo, channel(bins[k], c));
                    hi = std::max(hi, channel(bins[k], c));
                }
                if (hi - lo > best_range)
                {
                    best_range = hi - lo;
                    best = i;
                    best_channel = c;
                }
            }
        }
        if (best == boxes.size())
            break;
        Box box = boxes[best];
        std::stable_sort(bins.begin() + (long)box.first, bins.begin() + (long)box.last, [&](const Bin &a, const Bin &b) {
            const int ca = channel(a, best_channel), cb = channel(b, best_channel);
            if (ca != cb)
                return ca < cb;
            return (a.r * 1024 + a.g * 32 + a.b) < (b.r * 1024 + b.g * 32 + b.b);
        });
        uint64_t half = 0, acc = 0;
        for (size_t k = box.first; k < box.last; k++)
            half += bins[k].count;
        half = (half + 1) / 2;
        size_t split = box.first + 1;
        for (size_t k = box.first; k < box.last - 1; k++)
        {
            acc += bins[k].count;
            split = k + 1;
            if (acc >= half)
                break;
        }
        uint64_t left = 0;
        for (size_t k = box.first; k < split; k++)
            left += bins[k].count;
        boxes[best] = {box.first, split, left};
        boxes.push_back({split, box.last, box.count - left});
    }

    // Palette: each box's weighted mean, 6-bit.
    std::vector<int> bin_to_entry(32768, -1);
    std::fill(out.palette, out.palette + LANDVIEW_PALETTE_SIZE, (uint8_t)0);
    for (size_t i = 0; i < boxes.size(); i++)
    {
        uint64_t r = 0, g = 0, b = 0, c = 0;
        for (size_t k = boxes[i].first; k < boxes[i].last; k++)
        {
            r += (uint64_t)bins[k].r * bins[k].count;
            g += (uint64_t)bins[k].g * bins[k].count;
            b += (uint64_t)bins[k].b * bins[k].count;
            c += bins[k].count;
            bin_to_entry[((size_t)bins[k].r << 10) | ((size_t)bins[k].g << 5) | (size_t)bins[k].b] = (int)i;
        }
        if (c == 0)
            continue;
        // 5-bit mean -> 6-bit: x * 63 / 31.
        out.palette[i * 3] = (uint8_t)((r * 63 + c * 31 / 2) / (c * 31));
        out.palette[i * 3 + 1] = (uint8_t)((g * 63 + c * 31 / 2) / (c * 31));
        out.palette[i * 3 + 2] = (uint8_t)((b * 63 + c * 31 / 2) / (c * 31));
    }
    out.width = width;
    out.height = height;
    out.pixels.resize(n);
    for (size_t i = 0; i < n; i++)
    {
        const size_t k = ((size_t)(rgba[i * 4] >> 3) << 10) | ((size_t)(rgba[i * 4 + 1] >> 3) << 5) | (size_t)(rgba[i * 4 + 2] >> 3);
        out.pixels[i] = (uint8_t)bin_to_entry[k];
    }
}

bool landview_decode_png(const uint8_t *data, size_t len, LandviewImage &out, std::string &error)
{
    spng_ctx *ctx = spng_ctx_new(0);
    if (ctx == nullptr)
    {
        error = "could not start the PNG decoder";
        return false;
    }
    struct Guard { spng_ctx *c; ~Guard() { spng_ctx_free(c); } } guard{ctx};
    spng_set_png_buffer(ctx, data, len);
    struct spng_ihdr ihdr;
    int r = spng_get_ihdr(ctx, &ihdr);
    if (r != 0)
    {
        error = std::string("not a readable PNG: ") + spng_strerror(r);
        return false;
    }
    if (ihdr.width != LANDVIEW_WIDTH || ihdr.height != LANDVIEW_HEIGHT)
    {
        error = "the image is " + std::to_string(ihdr.width) + " x " + std::to_string(ihdr.height) + ", it must be "
            + std::to_string(LANDVIEW_WIDTH) + " x " + std::to_string(LANDVIEW_HEIGHT);
        return false;
    }
    if (ihdr.color_type == SPNG_COLOR_TYPE_INDEXED && ihdr.bit_depth == 8)
    {
        struct spng_plte plte;
        std::memset(&plte, 0, sizeof(plte));
        if (spng_get_plte(ctx, &plte) != 0)
        {
            error = "the indexed PNG has no palette";
            return false;
        }
        size_t size = 0;
        spng_decoded_image_size(ctx, SPNG_FMT_PNG, &size);
        if (size != (size_t)LANDVIEW_PIXELS)
        {
            error = "unexpected indexed image size";
            return false;
        }
        out.pixels.resize(size);
        r = spng_decode_image(ctx, out.pixels.data(), size, SPNG_FMT_PNG, 0);
        if (r != 0)
        {
            error = std::string("could not decode the PNG: ") + spng_strerror(r);
            return false;
        }
        std::fill(out.palette, out.palette + LANDVIEW_PALETTE_SIZE, (uint8_t)0);
        for (uint32_t i = 0; i < plte.n_entries && i < 256; i++)
        {
            out.palette[i * 3] = (uint8_t)(plte.entries[i].red >> 2);
            out.palette[i * 3 + 1] = (uint8_t)(plte.entries[i].green >> 2);
            out.palette[i * 3 + 2] = (uint8_t)(plte.entries[i].blue >> 2);
        }
        out.width = LANDVIEW_WIDTH;
        out.height = LANDVIEW_HEIGHT;
        return true;
    }
    size_t size = 0;
    r = spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &size);
    if (r != 0)
    {
        error = std::string("could not read the PNG: ") + spng_strerror(r);
        return false;
    }
    std::vector<uint8_t> rgba(size);
    r = spng_decode_image(ctx, rgba.data(), size, SPNG_FMT_RGBA8, 0);
    if (r != 0)
    {
        error = std::string("could not decode the PNG: ") + spng_strerror(r);
        return false;
    }
    landview_quantise(rgba.data(), LANDVIEW_WIDTH, LANDVIEW_HEIGHT, out);
    return true;
}

bool landview_load(const std::string &base, LandviewImage &out, std::string &error, bool *used_png)
{
    if (used_png != nullptr)
        *used_png = false;
    std::vector<uint8_t> bytes;
    if (slurp_file(base + ".png", bytes))
    {
        if (used_png != nullptr)
            *used_png = true;
        return landview_decode_png(bytes.data(), bytes.size(), out, error);
    }
    std::vector<uint8_t> pal;
    // The .raw may be RNC-compressed, as the original game's are: the game's own loader unpacks it.
    const std::string raw = base + ".raw";
    const int64_t flen = LbFileLengthRnc(raw.c_str());
    if (flen < 0 || !slurp_file(base + ".pal", pal))
    {
        error = "neither " + base + ".png nor " + base + ".raw and .pal exist";
        return false;
    }
    if (flen != (int64_t)LANDVIEW_PIXELS)
    {
        error = base + ".raw is not a 1280 x 960 image";
        return false;
    }
    bytes.assign((size_t)flen, 0);
    if (LbFileLoadAt(raw.c_str(), bytes.data()) != flen)
    {
        error = "could not read " + raw;
        return false;
    }
    if (pal.size() != (size_t)LANDVIEW_PALETTE_SIZE)
    {
        error = base + ".pal is not 768 bytes";
        return false;
    }
    out.width = LANDVIEW_WIDTH;
    out.height = LANDVIEW_HEIGHT;
    out.pixels = bytes;
    std::copy(pal.begin(), pal.end(), out.palette);
    return true;
}

std::vector<uint8_t> landview_to_rgba(const LandviewImage &img)
{
    std::vector<uint8_t> out(img.pixels.size() * 4);
    for (size_t i = 0; i < img.pixels.size(); i++)
    {
        const uint8_t *p = &img.palette[img.pixels[i] * 3];
        out[i * 4] = (uint8_t)((p[0] << 2) | (p[0] >> 4));
        out[i * 4 + 1] = (uint8_t)((p[1] << 2) | (p[1] >> 4));
        out[i * 4 + 2] = (uint8_t)((p[2] << 2) | (p[2] >> 4));
        out[i * 4 + 3] = 255;
    }
    return out;
}

extern "C" int landview_load_png_indexed(const char *base, uint8_t *pixels, uint8_t *palette)
{
    std::vector<uint8_t> bytes;
    const std::string b = base;
    if (!slurp_file(b + ".png", bytes))
        return 0;
    LandviewImage img;
    std::string error;
    if (!landview_decode_png(bytes.data(), bytes.size(), img, error))
    {
        ERRORLOG("Land Map background \"%s.png\": %s", base, error.c_str());
        return -1;
    }
    std::memcpy(pixels, img.pixels.data(), (size_t)LANDVIEW_PIXELS);
    std::memcpy(palette, img.palette, LANDVIEW_PALETTE_SIZE);
    return 1;
}
