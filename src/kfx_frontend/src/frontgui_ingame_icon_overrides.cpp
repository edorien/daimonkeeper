#include "pre_inc.h"
#include "frontgui_ingame_icon_overrides.h"

#include "config.h"                    // prepare_file_fmtpath, FGrp_FxData
#include "config_keeperfx.h"           // keeperfx_ui_config.gui_icon_pack
#include "bflib_fileio.h"               // LbFileExists/Open/Read/Close/Length
#include "renderer/RendererManager.h"   // RendererCreateDynamicTexture/Update/Destroy
#include "platform/PlatformManager.h"   // PlatformManager_ListSubdirectories -- case-insensitive pack-dir resolve
#include "sprites.h"                    // GPS_*/GBS_* -- the static override table

#include "post_inc.h"

#include <spng.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h> // strcasecmp
#include <map>
#include <string>
#include <vector>

namespace {

struct OverrideTex {
    void *texture = nullptr;
    int64_t   width   = 0;
    int64_t   height  = 0;
    bool  attempted = false; // true once a decode has been tried, success or not
};

std::map<std::string, OverrideTex> s_cache;
char s_loaded_pack[64] = "";

std::string lower(const char *s)
{
    std::string out(s != nullptr ? s : "");
    for (char &c : out)
        c = (char)std::tolower((unsigned char)c);
    return out;
}

// `<category>_<code_name>`, lower-cased -- except some categories' own
// code_name already carries the category as a prefix (PowerConfigStats::
// code_name is "POWER_LIGHTNING", not "LIGHTNING") -- concatenating
// blindly there would ask for "power_power_lightning...", which no pack
// author would ever write. Detect and skip the redundant prefix instead.
void build_base_name(const char *category, const char *code_name, char *out, size_t out_size)
{
    const std::string cn = lower(code_name);
    const std::string prefix = std::string(category) + "_";
    if (cn.rfind(prefix, 0) == 0)
        std::snprintf(out, out_size, "%s", cn.c_str());
    else
        std::snprintf(out, out_size, "%s_%s", category, cn.c_str());
}

char s_pack_dir[64] = ""; // actual on-disk casing of the active pack's sub-directory

// Windows' filesystem is case-insensitive; Linux/macOS's is not, but
// GUI_ICON_PACK's own enum matching already treats the setting as
// case-insensitive (get_gui_icon_pack()/set_gui_icon_pack(),
// config_settingschema.c, strcasecmp -- matching UI_FONT's own
// case-insensitive family-name matching). Building the PNG path straight
// from keeperfx_ui_config.gui_icon_pack's literal case would silently
// find nothing on Linux/macOS the moment the config value's case doesn't
// match the folder's own casing (live-tested: "not seeing them change"
// traced partly to exactly this) -- so resolve the *actual* on-disk
// directory name once per pack switch instead of trusting the setting's
// spelling.
void resolve_pack_dir_casing()
{
    std::snprintf(s_pack_dir, sizeof(s_pack_dir), "%s", s_loaded_pack);
    char *gui_dir = prepare_file_path(FGrp_FxData, "gui");
    if (gui_dir == nullptr)
        return;
    char gui_dir_copy[400];
    std::snprintf(gui_dir_copy, sizeof(gui_dir_copy), "%s", gui_dir);

    const int64_t max_entries = 64;
    const int64_t name_len = 64;
    std::vector<char> subs((size_t)max_entries * name_len);
    int64_t count = PlatformManager_ListSubdirectories(gui_dir_copy, subs.data(), name_len, max_entries);
    for (int64_t i = 0; i < count; i++)
    {
        const char *sub = subs.data() + (size_t)i * name_len;
        if (strcasecmp(sub, s_loaded_pack) == 0)
        {
            std::snprintf(s_pack_dir, sizeof(s_pack_dir), "%s", sub);
            return;
        }
    }
}

// Mirrors frontgui_style.cpp's refresh_fonts_if_changed() -- GUI_ICON_PACK
// is SApply_Live, so this module polls the config value itself rather than
// needing a bespoke apply callback. Destroys every cached override texture
// so a stale pack's images never bleed into a newly-selected one.
void ensure_pack_current()
{
    if (std::strcmp(s_loaded_pack, keeperfx_ui_config.gui_icon_pack) == 0)
        return;
    std::snprintf(s_loaded_pack, sizeof(s_loaded_pack), "%s", keeperfx_ui_config.gui_icon_pack);
    for (auto &kv : s_cache)
        if (kv.second.texture != nullptr)
            RendererDestroyDynamicTexture(kv.second.texture);
    s_cache.clear();

    // One line per pack switch (not per-lookup -- those stay silent, doc
    // §6.5) so a pack author can confirm from keeperfx.log which folder is
    // actually active, and where it resolved to on disk.
    if (strcasecmp(s_loaded_pack, "NONE") != 0)
    {
        resolve_pack_dir_casing();
        char *dir = prepare_file_fmtpath(FGrp_FxData, "gui/%s", s_pack_dir);
        JUSTLOG("GUI icon pack '%s' active (%s)", s_loaded_pack, dir != nullptr ? dir : "?");
    }
}

bool pack_is_none()
{
    return strcasecmp(keeperfx_ui_config.gui_icon_pack, "NONE") == 0;
}

// Plain RGBA8 decode -- deliberately not custom_sprites.c's
// decode_png_to_sprite() (that one quantizes into the legacy paletted
// sprite format for the classic renderer + mod system; this feature wants
// exactly what ImGui/RendererCreateDynamicTexture already wants, no
// palette matching at all). Same spng call shape custom_sprites.c already
// uses for its own buffer-based decode (process_icon_from_list's zip path).
bool decode_png_rgba(const char *path, std::vector<unsigned char> *out, int64_t *out_w, int64_t *out_h)
{
    int64_t fsize = LbFileLength(path);
    if (fsize <= 0)
        return false;
    TbFileHandle fh = LbFileOpen(path, Lb_FILE_MODE_READ_ONLY);
    if (fh == NULL)
        return false;
    std::vector<unsigned char> file_buf((size_t)fsize);
    int64_t rlen = (int64_t)LbFileRead(fh, file_buf.data(), (uint64_t)fsize);
    LbFileClose(fh);
    if (rlen != fsize)
        return false;

    spng_ctx *ctx = spng_ctx_new(0);
    if (ctx == nullptr)
        return false;
    spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    const size_t limit = 1024 * 1024 * 4;
    spng_set_chunk_limits(ctx, limit, limit);

    if (spng_set_png_buffer(ctx, file_buf.data(), file_buf.size()))
    {
        spng_ctx_free(ctx);
        return false;
    }

    struct spng_ihdr ihdr;
    if (spng_get_ihdr(ctx, &ihdr) || ihdr.width <= 0 || ihdr.height <= 0
     || ihdr.width > 4096 || ihdr.height > 4096)
    {
        spng_ctx_free(ctx);
        return false;
    }

    size_t rgba_size = 0;
    if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &rgba_size))
    {
        spng_ctx_free(ctx);
        return false;
    }
    out->resize(rgba_size);
    if (spng_decode_image(ctx, out->data(), rgba_size, SPNG_FMT_RGBA8, SPNG_DECODE_TRNS))
    {
        spng_ctx_free(ctx);
        return false;
    }
    spng_ctx_free(ctx);

    *out_w = (int64_t)ihdr.width;
    *out_h = (int64_t)ihdr.height;
    return true;
}

// Shared resolver: `name` is a fully-assembled, already-lowercased
// friendly name. Every public entry point in this file funnels here so
// there is exactly one cache and one decode path.
void *resolve(const char *name, int64_t *out_w, int64_t *out_h)
{
    ensure_pack_current();
    if (out_w != nullptr) *out_w = 0;
    if (out_h != nullptr) *out_h = 0;
    if (pack_is_none())
        return nullptr;

    OverrideTex &e = s_cache[name];
    if (!e.attempted)
    {
        e.attempted = true;
        char *path = prepare_file_fmtpath(FGrp_FxData, "gui/%s/%s.png", s_pack_dir, name);
        if (path != nullptr && LbFileExists(path))
        {
            char path_copy[512];
            std::snprintf(path_copy, sizeof(path_copy), "%s", path);
            std::vector<unsigned char> rgba;
            int64_t w = 0, h = 0;
            if (decode_png_rgba(path_copy, &rgba, &w, &h))
            {
                void *tex = RendererCreateDynamicTexture(w, h);
                if (tex != nullptr)
                {
                    RendererUpdateDynamicTexture(tex, rgba.data(), w, h);
                    e.texture = tex;
                    e.width   = w;
                    e.height  = h;
                }
                else
                {
                    WARNLOG("GUI icon override '%s': texture upload failed", path_copy);
                }
            }
            else
            {
                // The file exists (LbFileExists above) but failed to
                // decode -- a genuine bad-file case (doc §6.2), distinct
                // from the silent common case of no matching file at all.
                WARNLOG("GUI icon override '%s': failed to decode PNG", path_copy);
            }
        }
    }
    if (e.texture != nullptr)
    {
        if (out_w != nullptr) *out_w = e.width;
        if (out_h != nullptr) *out_h = e.height;
    }
    return e.texture;
}

// Static category table (doc §3.1) -- one row per fixed, compile-time
// GPS_*/GBS_* icon this project draws from a static constant rather than
// a per-kind config field. Deliberately does NOT include the colorized
// tab_room/tab_creature icons (get_player_colored_icon_idx() resolves to
// a different actual sprite index per player colour, so a single
// index-keyed row can't represent them without also deciding what happens
// to the tint -- flagged as a follow-up, not silently forgotten) or the
// ~20 stat_* icons (creature_query_panel()'s Stats page, vertical layout
// only -- lower value, deferred to keep this table's first landing small).
struct StaticRow { int64_t idx; bool button_sheet; const char *name; };
const StaticRow s_static_rows[] = {
    { GPS_rpanel_tab_crtr_wandr_act,   false, "job_idle" },
    { GPS_rpanel_tab_crtr_work_act,    false, "job_work" },
    { GPS_rpanel_tab_crtr_fight_act,   false, "job_fight" },
    { GPS_rpanel_tendency_prisnd_act,  false, "tendency_imprison_active" },
    { GPS_rpanel_tendency_prisnu_dis,  false, "tendency_imprison_inactive" },
    { GPS_rpanel_tendency_fleed_act,   false, "tendency_flee" },
    // bar_research / bar_workshop also alias the spell/trap tab-header
    // icons and the matching event-marker icons (frontgui_ingame_panel.cpp)
    // -- the legacy atlas draws literally the same picture in all three
    // spots, so one override row covers all three by construction; see
    // doc §2's "aliasing" note.
    { GPS_room_treasury_std_s,         false, "bar_payday" },
    { GPS_room_research_std_s,         false, "bar_research" },
    { GPS_room_workshop_std_s,         false, "bar_workshop" },
    { GBS_guisymbols_sym_fight,        true,  "battle_vs" }, // also aliases combat event markers
    { GBS_options_button_smd_yes,      true,  "confirm_yes" },
    { GBS_options_button_smd_no,       true,  "confirm_no" },
    { GBS_options_button_load,         true,  "launcher_load" },
    { GBS_options_button_save,         true,  "launcher_save" },
    { GBS_options_button_graphc,       true,  "launcher_options" },
    { GBS_options_button_exit,         true,  "launcher_quit" },
};

} // namespace

void *FeIconOverrideTexture(const char *name, int64_t *out_w, int64_t *out_h)
{
    return resolve(lower(name).c_str(), out_w, out_h);
}

void *FeIconOverrideActiveInactive(const char *category, const char *code_name, bool active,
                                   int64_t *out_w, int64_t *out_h, bool *out_dim)
{
    if (out_dim != nullptr) *out_dim = false;
    char base[80];
    build_base_name(category, code_name, base, sizeof(base));

    // "_active"/"_inactive" first, then the legacy DK "_std"/"_dis"
    // spelling many existing fan icon packs already use (live-tested: a
    // hand-built pack used _std/_dis throughout) -- accepting both means
    // a pack author can use whichever convention they already know.
    const char *primary = active ? "active" : "inactive";
    const char *legacy  = active ? "std" : "dis";
    char variant[96];
    std::snprintf(variant, sizeof(variant), "%s_%s", base, primary);
    void *tex = resolve(variant, out_w, out_h);
    if (tex == nullptr)
    {
        std::snprintf(variant, sizeof(variant), "%s_%s", base, legacy);
        tex = resolve(variant, out_w, out_h);
    }
    if (tex != nullptr || active)
        return tex;

    // No "_inactive"/"_dis" file -- fall back to "_active"/"_std" and let
    // the caller dim it, matching the legacy sprite path's own alpha-dim
    // treatment (doc §3.3).
    std::snprintf(variant, sizeof(variant), "%s_active", base);
    tex = resolve(variant, out_w, out_h);
    if (tex == nullptr)
    {
        std::snprintf(variant, sizeof(variant), "%s_std", base);
        tex = resolve(variant, out_w, out_h);
    }
    if (out_dim != nullptr) *out_dim = (tex != nullptr);
    return tex;
}

void *FeIconOverrideSingle(const char *category, const char *code_name, int64_t *out_w, int64_t *out_h)
{
    char name[80];
    build_base_name(category, code_name, name, sizeof(name));
    return resolve(name, out_w, out_h);
}

void *FeIconOverrideForStaticIndex(int64_t sprite_idx, bool is_button_sheet, int64_t *out_w, int64_t *out_h)
{
    ensure_pack_current();
    if (pack_is_none())
        return nullptr;
    for (const StaticRow &row : s_static_rows)
        if (row.idx == sprite_idx && row.button_sheet == is_button_sheet)
            return resolve(row.name, out_w, out_h);
    return nullptr;
}
