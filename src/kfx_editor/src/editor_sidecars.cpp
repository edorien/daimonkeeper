/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_sidecars.cpp
 *     See editor_sidecars.h.
 */
#include <inttypes.h>
#include "pre_inc.h"
#include "editor_sidecars.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include "post_inc.h"

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string strip_trailing_slashes(std::string s)
{
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    return s;
}

} // namespace

bool editor_sidecar_ext_is_handled(const std::string &ext_in)
{
    static const char *const handled[] = {
        // Written by MapContentWriter (either format).
        "slb", "own", "inf", "slx", "txt", "lua", "tng", "tngfx", "lgt", "lgtfx", "apt", "aptfx", "lof", "lif",
        // Derived by the loader on demand (regenerate_derived_map_data), or
        // authoring-tool leftovers with no effect on the game.
        "dat", "clm", "wib", "wlb", "une", "vsn", "adi", "flg", "nfo", "bak",
    };
    const std::string ext = lower(ext_in);
    for (const char *h : handled)
        if (ext == h)
            return true;
    return false;
}

std::vector<std::string> editor_find_sidecars(const char *dir, uint64_t lvnum)
{
    std::vector<std::string> found;
    if (dir == nullptr || dir[0] == '\0')
        return found;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "map%05" PRIu64 ".", (uint64_t)(lvnum));
    const std::string pfx = prefix;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = it->path().filename().string();
        if (name.compare(0, pfx.size(), pfx) != 0)
            continue;
        if (!editor_sidecar_ext_is_handled(name.substr(pfx.size())))
            found.push_back(name);
    }
    std::sort(found.begin(), found.end());
    return found;
}

bool editor_save_is_relocation(const char *src_dir, uint64_t src_lvnum,
    const char *dst_dir, uint64_t dst_lvnum)
{
    if (src_lvnum != dst_lvnum)
        return true;
    return strip_trailing_slashes(src_dir != nullptr ? src_dir : "")
        != strip_trailing_slashes(dst_dir != nullptr ? dst_dir : "");
}

int64_t editor_remove_sidecars(const char *dir, uint64_t lvnum)
{
    int64_t removed = 0;
    for (const std::string &name : editor_find_sidecars(dir, lvnum))
    {
        std::error_code ec;
        if (std::filesystem::remove(std::filesystem::path(dir) / name, ec) && !ec)
            removed++;
    }
    return removed;
}

int64_t editor_copy_sidecars(const char *src_dir, uint64_t src_lvnum,
    const char *dst_dir, uint64_t dst_lvnum)
{
    char src_prefix[32];
    char dst_prefix[32];
    snprintf(src_prefix, sizeof(src_prefix), "map%05" PRIu64, (uint64_t)(src_lvnum));
    snprintf(dst_prefix, sizeof(dst_prefix), "map%05" PRIu64, (uint64_t)(dst_lvnum));
    int64_t copied = 0;
    for (const std::string &name : editor_find_sidecars(src_dir, src_lvnum))
    {
        const std::string target = dst_prefix + name.substr(strlen(src_prefix));
        std::error_code ec;
        std::filesystem::copy_file(std::filesystem::path(src_dir) / name, std::filesystem::path(dst_dir) / target,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec)
            copied++;
    }
    return copied;
}
