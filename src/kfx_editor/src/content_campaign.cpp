/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_campaign.cpp
 *     The Campaign editor window. See content_campaign.h.
 */
#include "pre_inc.h"
#include "content_campaign.h"
#include "content_picker.h"
#include "content_target.h"
#include "frontend.h"
#include "content_campaign_ops.h"
#include "landview_image.h"
#include "content_raw.h"
#include "content_tools.h"
#include "content_picker.h"
#include "renderer/RendererManager.h"
#include "editor_dialogs.h"
#include "content_form.h"
#include "cfgc_campaign_check.h"
#include "cfgc_campaign_edit.h"
#include "cfgc_campaign_levels.h"
#include "cfgc_writer.h"
#include "cfgc_content.h"
#include "frontgui_widgets.h"

#include <imgui.h>

#include "config.h"
#include "config_campaigns.h"
#include "config_keeperfx.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "post_inc.h"

namespace fs = std::filesystem;

/******************************************************************************/
namespace {

struct LevelRow
{
    int64_t number = 0;
    CampaignListKind kind = CampList_Single;
    size_t index = 0;   // position in its list (a bonus level: the single level it belongs to)
    std::string list;   // "Single", "Bonus", "Extra"
    std::string name;
    std::string players;
    std::string ensign;
    bool has_entry = false;
    bool has_map = false;
};

// Pending edits in the order they were made (new level entries then come out in that order, not sorted by text).
struct EditList
{
    std::vector<std::pair<std::string, std::string>> items;
    bool empty() const { return items.empty(); }
    void clear() { items.clear(); }
    std::string &operator[](const std::string &k)
    {
        for (auto &i : items)
            if (i.first == k)
                return i.second;
        items.emplace_back(k, std::string());
        return items.back().second;
    }
    size_t erase(const std::string &k)
    {
        for (size_t i = 0; i < items.size(); i++)
            if (items[i].first == k)
            {
                items.erase(items.begin() + (long)i);
                return 1;
            }
        return 0;
    }
    size_t count(const std::string &k) const
    {
        for (const auto &i : items)
            if (i.first == k)
                return 1;
        return 0;
    }
    std::vector<std::pair<std::string, std::string>>::const_iterator find(const std::string &k) const
    {
        for (auto it = items.begin(); it != items.end(); ++it)
            if (it->first == k)
                return it;
        return items.end();
    }
    std::vector<std::pair<std::string, std::string>>::const_iterator begin() const { return items.begin(); }
    std::vector<std::pair<std::string, std::string>>::const_iterator end() const { return items.end(); }
};

struct CampaignState
{
    bool open = false;
    bool map_host = false;
    std::vector<ContentCampaign> campaigns; // the campaigns (not the packs) the game knows
    std::vector<ContentCampaign> everyone;  // every campaign and pack, for the sharing test
    int64_t idx = 0;
    ConfigDocument doc;
    ConfigContent content; // the file as saved
    ConfigDocument preview; // the file with the pending edits
    ConfigContent view;     // its content: what the pages show
    std::vector<CfgDiagnostic> findings;
    std::vector<LevelRow> levels;
    std::string status;
    bool loaded = false;
    // Unapplied edits: "block|KEY" (block "common", "strings", "speech"; KEY upper-case) -> new value; a removed key
    // is in `removed` instead.
    EditList edits;
    std::set<std::string> removed;
    std::string reload_fname; // campaign to select again after a rescan
    std::string last_fname;   // the campaign shown last: the window opens on it again
    int64_t sel = -1;         // selected row of `levels`
    int64_t add_pick = 0;     // index into the unlisted map files
    // Config files page: the campaign's files (cached; rebuilt after a change).
    struct FileRow
    {
        RawFileEntry file;
        bool has = false;       // the campaign layer has the file
        int64_t size = 0;
        size_t keys = 0;
    };
    std::vector<FileRow> files;
    bool files_stale = true;
    bool files_only_own = true;
    std::string land_scan_dir;                 // the folder the two lists below were read from
    std::vector<std::string> land_images;      // rgmapNN stems (.raw or .png) in the land folder
    std::vector<std::string> land_frames;      // viframeNN stems
    // Land view page: the picture shown (loaded lazily), which picture / position the clicks set.
    void *land_tex = nullptr;
    std::string land_key;
    std::string land_error;
    std::string land_info;
    int64_t land_mode = 0; // 0 = ensign position on the overview, 1 = ensign zoom position on the level's own picture
    char new_name[128] = "";
    char new_id[64] = "";
    bool new_id_touched = false;
    int64_t new_human = 0;
    bool new_own_config = true;
    std::string new_error;
    bool confirm_remove = false;
    bool remove_entry = true;
};

CampaignState s_cs;

std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const CfgContentSection *common()
{
    return s_cs.content.find_section("common");
}

std::string common_value(const char *key)
{
    if (const CfgContentSection *s = common())
        if (const std::string *v = s->last_value(key))
            return *v;
    return std::string();
}

std::string five(int64_t n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%05lld", (long long)n);
    return buf;
}

std::string collapse(const std::string &s)
{
    std::istringstream in(s);
    std::string out;
    for (std::string w; in >> w;)
        out += (out.empty() ? "" : " ") + w;
    return out;
}

CampaignCheckEnv make_env(const ContentCampaign &camp)
{
    CampaignCheckEnv env;
    env.root = content_root();
    env.own_fname = camp.fname;
    env.file_exists = [](const std::string &p) { std::error_code ec; return fs::is_regular_file(p, ec); };
    env.dir_exists = [](const std::string &p) { std::error_code ec; return fs::is_directory(p, ec); };
    env.file_size = [](const std::string &p) {
        std::error_code ec;
        const auto n = fs::file_size(p, ec);
        return ec ? (int64_t)-1 : (int64_t)n;
    };
    env.read_prefix = [](const std::string &p, size_t n) {
        std::ifstream f(p, std::ios::binary);
        std::string head(n, '\0');
        f.read(&head[0], (std::streamsize)n);
        head.resize((size_t)f.gcount());
        return head;
    };
    for (const ContentCampaign &c : s_cs.everyone)
        env.peers.push_back({c.name, c.fname, c.cfg_dir, c.crtr_dir});
    return env;
}

void rebuild_view(const ContentCampaign &camp);

void fill_row(LevelRow &r, const ContentCampaign &camp)
{
    r.has_map = !camp.levels_dir.empty() && fs::is_regular_file(camp.levels_dir + "/map" + five(r.number) + ".slb");
    if (const CfgContentSection *e = s_cs.view.find_section("map", r.number))
    {
        r.has_entry = true;
        if (const std::string *v = e->last_value("NAME_TEXT"))
            r.name = *v;
        else if (const std::string *v2 = e->last_value("NAME_ID"))
            r.name = "string " + collapse(*v2);
        if (const std::string *v = e->last_value("PLAYERS"))
            r.players = collapse(*v);
        const std::string *en = e->last_value("ENSIGN");
        if (en == nullptr)
            en = e->last_value("OPTIONS");
        if (en != nullptr)
            r.ensign = collapse(*en);
    }
}

// The rows of the levels table: each single level (its bonus level under it), then the extra levels.
void rebuild_rows(const ContentCampaign &camp)
{
    s_cs.levels.clear();
    const CampaignLevels lv = cfgc_read_levels(s_cs.view);
    for (size_t i = 0; i < lv.single.size(); i++)
    {
        LevelRow r;
        r.number = lv.single[i];
        r.kind = CampList_Single;
        r.index = i;
        r.list = "Single";
        fill_row(r, camp);
        s_cs.levels.push_back(r);
        if (lv.bonus[i] != 0)
        {
            LevelRow b;
            b.number = lv.bonus[i];
            b.kind = CampList_Bonus;
            b.index = i;
            b.list = "  Bonus";
            fill_row(b, camp);
            s_cs.levels.push_back(b);
        }
    }
    for (size_t i = 0; i < lv.extra.size(); i++)
    {
        LevelRow r;
        r.number = lv.extra[i];
        r.kind = CampList_Extra;
        r.index = i;
        r.list = "Extra";
        fill_row(r, camp);
        s_cs.levels.push_back(r);
    }
    if (s_cs.sel >= (int64_t)s_cs.levels.size())
        s_cs.sel = (int64_t)s_cs.levels.size() - 1;
}

void load()
{
    s_cs.loaded = true;
    s_cs.levels.clear();
    s_cs.findings.clear();
    s_cs.content = ConfigContent();
    s_cs.doc = ConfigDocument();
    if (s_cs.campaigns.empty())
        return;
    s_cs.idx = std::max<int64_t>(0, std::min<int64_t>(s_cs.idx, (int64_t)s_cs.campaigns.size() - 1));
    const ContentCampaign &camp = s_cs.campaigns[(size_t)s_cs.idx];
    s_cs.last_fname = camp.fname;
    if (camp.cfg_file.empty() || !fs::is_regular_file(camp.cfg_file))
    {
        s_cs.status = "The campaign's .cfg file could not be found.";
        return;
    }
    s_cs.doc = ConfigDocument::parse(slurp(camp.cfg_file));
    s_cs.content = read_config_content(s_cs.doc, "campaign", false);
    s_cs.edits.clear();
    s_cs.removed.clear();

    s_cs.sel = -1;
    s_cs.files_stale = true;
    s_cs.land_scan_dir.clear();
    rebuild_view(camp);
    s_cs.status.clear();
}

std::string upper(std::string k)
{
    for (char &c : k)
        c = (char)std::toupper((unsigned char)c);
    return k;
}

bool is_level_list(const std::string &k)
{
    return k == "SINGLE_LEVELS" || k == "BONUS_LEVELS" || k == "EXTRA_LEVELS";
}

bool dirty()
{
    return !s_cs.edits.empty() || !s_cs.removed.empty();
}

// The change set of the pending edits.
ChangeSet pending_changes()
{
    ChangeSet cs;
    for (const auto &e : s_cs.edits)
    {
        const size_t bar = e.first.find('|');
        cs.set(e.first.substr(0, bar), e.first.substr(bar + 1), e.second);
    }
    for (const std::string &r : s_cs.removed)
    {
        const size_t bar = r.find('|');
        if (bar + 1 == r.size())
            cs.reset_section(r.substr(0, bar)); // "block|": the whole block
        else
            cs.reset(r.substr(0, bar), r.substr(bar + 1));
    }
    return cs;
}

// Recomputes the preview (the file with the pending edits), the content the pages show and the findings.
void rebuild_view(const ContentCampaign &camp)
{
    s_cs.preview = s_cs.doc;
    if (dirty())
        cfgc_make_writer(build_engine_schema(), "campaign")->apply(s_cs.preview, pending_changes(), nullptr);
    s_cs.view = read_config_content(s_cs.preview, "campaign", false);
    s_cs.findings = cfgc_check_campaign(s_cs.preview, build_engine_schema(), make_env(camp));
    rebuild_rows(camp);
}

void refresh_findings(const ContentCampaign &camp)
{
    rebuild_view(camp);
}

// Queues a change set as pending edits.
void stage(const ContentCampaign &camp, const ChangeSet &cs)
{
    for (const CfgChange &c : cs.changes)
    {
        const std::string id = c.section + "|" + (c.kind == CfgChange::ResetSection ? std::string() : upper(c.key));
        if (c.kind == CfgChange::Set)
        {
            std::string v;
            for (const std::string &w : c.values)
                v += (v.empty() ? "" : " ") + w;
            s_cs.removed.erase(id);
            s_cs.edits[id] = v;
        }
        else
        {
            s_cs.edits.erase(id);
            if (c.kind == CfgChange::ResetSection)
            {
                auto &v = s_cs.edits.items;
                v.erase(std::remove_if(v.begin(), v.end(), [&](const std::pair<std::string, std::string> &e) {
                    return e.first.compare(0, c.section.size() + 1, c.section + "|") == 0;
                }), v.end());
            }
            s_cs.removed.insert(id);
        }
    }
    rebuild_view(camp);
}

void set_pending(const ContentCampaign &camp, const std::string &block, const std::string &key, const std::string &value)
{
    const std::string id = block + "|" + upper(key);
    s_cs.removed.erase(id);
    s_cs.edits[id] = value;
    refresh_findings(camp);
}

void remove_pending(const ContentCampaign &camp, const std::string &block, const std::string &key)
{
    const std::string id = block + "|" + upper(key);
    s_cs.edits.erase(id);
    s_cs.removed.insert(id);
    refresh_findings(camp);
}

void discard_pending(const ContentCampaign &camp)
{
    s_cs.edits.clear();
    s_cs.removed.clear();
    refresh_findings(camp);
}

// The rows of one block: the file's keys (plus keys added here), minus the ones removed here.
struct KeyRow
{
    std::string key;
    std::string value;
    bool changed = false;
};

std::vector<KeyRow> rows_of(const char *block)
{
    std::vector<KeyRow> rows;
    std::set<std::string> seen;
    if (const CfgContentSection *c = s_cs.content.find_section(block))
        for (const CfgField &f : c->fields)
        {
            const std::string k = upper(f.key);
            if (!seen.insert(k).second || s_cs.removed.count(std::string(block) + "|" + k))
                continue;
            KeyRow r;
            r.key = f.key;
            r.value = collapse(f.values.empty() ? std::string() : f.values.back());
            const auto e = s_cs.edits.find(std::string(block) + "|" + k);
            if (e != s_cs.edits.end())
            {
                r.value = e->second;
                r.changed = true;
            }
            rows.push_back(std::move(r));
        }
    for (const auto &e : s_cs.edits)
    {
        const size_t bar = e.first.find('|');
        if (e.first.substr(0, bar) == block && !seen.count(e.first.substr(bar + 1)))
        {
            KeyRow r;
            r.key = e.first.substr(bar + 1);
            r.value = e.second;
            r.changed = true;
            rows.push_back(std::move(r));
        }
    }
    return rows;
}

std::vector<std::string> options_of(const std::string &key)
{
    if (key == "HUMAN_PLAYER")
        return {"RED", "BLUE", "GREEN", "YELLOW", "WHITE", "PURPLE", "BLACK", "ORANGE"};
    if (key == "ASSIGN_CPU_KEEPERS")
        return {"ON", "OFF"};
    if (key == "LAND_MARKERS")
        return {"ENSIGNS", "PINPOINTS"};
    return {};
}

bool shared_location(const std::string &key)
{
    for (const CfgDiagnostic &d : s_cs.findings)
        if (d.code == "location_shared" && d.message.compare(0, key.size(), key) == 0)
            return true;
    return false;
}

// Draws one editable key row; returns true when the row was changed or removed (rows_of() is then stale).
bool draw_key_row(const ContentCampaign &camp, const char *block, const KeyRow &r, bool removable)
{
    bool changed = false;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (r.changed)
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", r.key.c_str());
    else
        ImGui::TextUnformatted(r.key.c_str());
    ImGui::TableNextColumn();
    ImGui::PushID((std::string(block) + r.key).c_str());
    const std::string k = upper(r.key);
    const std::vector<std::string> options = std::string(block) == "common" ? options_of(k) : std::vector<std::string>();
    if (is_level_list(k))
    {
        ImGui::TextUnformatted(r.value.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(edited on the Levels page)");
    }
    else if (!options.empty())
    {
        std::vector<const char *> ptrs;
        int64_t cur = -1;
        for (size_t i = 0; i < options.size(); i++)
        {
            ptrs.push_back(options[i].c_str());
            if (upper(r.value) == options[i])
                cur = (int64_t)i;
        }
        ImGui::SetNextItemWidth(form_col(14));
        if (FeCombo("##v", &cur, ptrs.data(), (int64_t)ptrs.size()) && cur >= 0)
        {
            set_pending(camp, block, r.key, options[(size_t)cur]);
            changed = true;
        }
    }
    else
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", r.value.c_str());
        ImGui::SetNextItemWidth(-form_col(16));
        if (ImGui::InputText("##v", buf, sizeof(buf)) && r.value != buf)
        {
            set_pending(camp, block, r.key, buf);
            changed = true;
        }
    }
    ImGui::TableNextColumn();
    if (is_level_list(k))
    {
    }
    else if (removable && FeButton("Remove", ImVec2(form_col(7), 0)))
    {
        remove_pending(camp, block, r.key);
        changed = true;
    }
    if (std::string(block) == "common" && k.size() > 9 && k.compare(k.size() - 9, 9, "_LOCATION") == 0 && !r.value.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", fs::is_directory(content_root() + "/" + r.value) ? "folder exists" : "folder missing",
            shared_location(k) ? ", shared" : "");
    }
    ImGui::PopID();
    return changed;
}

void draw_add_key(const ContentCampaign &camp, const char *block, const std::vector<std::string> &choices)
{
    std::vector<std::string> free_keys;
    std::set<std::string> have;
    for (const KeyRow &r : rows_of(block))
        have.insert(upper(r.key));
    for (const std::string &c : choices)
        if (!have.count(c) && !is_level_list(c))
            free_keys.push_back(c);
    if (free_keys.empty())
        return;
    std::vector<const char *> ptrs;
    for (const std::string &k : free_keys)
        ptrs.push_back(k.c_str());
    ImGui::PushID(block);
    int64_t pick = -1;
    ImGui::SetNextItemWidth(form_col(18));
    if (FeCombo("##add", &pick, ptrs.data(), (int64_t)ptrs.size()) && pick >= 0)
        set_pending(camp, block, free_keys[(size_t)pick], options_of(free_keys[(size_t)pick]).empty() ? std::string() : options_of(free_keys[(size_t)pick])[0]);
    ImGui::SameLine();
    FeCaption("Add a key");
    ImGui::PopID();
}

bool apply_own_config(const ContentCampaign &camp, std::string *error);

bool apply_pending(const ContentCampaign &camp, std::string *error)
{
    ConfigDocument doc = s_cs.doc;
    const auto writer = cfgc_make_writer(build_engine_schema(), "campaign");
    const ChangeResult res = writer->apply(doc, pending_changes(), nullptr);
    if (!res.ok)
    {
        *error = "The changes could not be prepared.";
        return false;
    }
    WriteBatch batch;
    batch.put(camp.cfg_file, doc.serialize());
    if (!batch.commit(error))
        return false;
    return true;
}

void rescan_lists()
{
    // The lists are read at start-up; read them again so the change shows without a restart.
    load_campaigns_list(&campaigns_list, FGrp_Campgn, "campaigns", "campgn_order.txt");
}

void reselect(const std::string &fname)
{
    s_cs.everyone = content_list_campaigns();
    s_cs.campaigns.clear();
    for (const ContentCampaign &c : s_cs.everyone)
        if (!c.is_mappack)
            s_cs.campaigns.push_back(c);
    for (size_t i = 0; i < s_cs.campaigns.size(); i++)
        if (s_cs.campaigns[i].fname == fname)
            s_cs.idx = (int64_t)i;
    s_cs.loaded = false;
}

// Copies the shared configuration / creature folders to the campaign's own and points the two keys at them.
bool apply_own_config(const ContentCampaign &camp, std::string *error)
{
    WriteBatch batch;
    ChangeSet cs;
    const struct { const char *key; const char *suffix; const std::string &dir; } locs[] = {
        {"CONFIGS_LOCATION", "cfg", camp.cfg_dir}, {"CREATURES_LOCATION", "crtr", camp.crtr_dir}};
    for (const auto &l : locs)
    {
        if (!shared_location(l.key))
            continue;
        const std::string current = common_value(l.key);
        const std::string own = cfgc_own_location(current, camp.fname, l.suffix);
        if (!cfgc_plan_folder_copy(l.dir, content_root() + "/" + own, batch, error))
            return false;
        cs.set("common", l.key, own);
    }
    if (cs.changes.empty())
        return true;
    ConfigDocument doc = s_cs.doc;
    cfgc_make_writer(build_engine_schema(), "campaign")->apply(doc, cs, nullptr);
    batch.put(camp.cfg_file, doc.serialize());
    return batch.commit(error);
}

void finish_write(const ContentCampaign &camp, const char *what)
{
    const std::string fname = camp.fname;
    s_cs.edits.clear();
    s_cs.removed.clear();
    rescan_lists();
    reselect(fname);
    s_cs.status = what;
}

void draw_menu_order(const ContentCampaign &camp);

void draw_identity(const ContentCampaign &camp)
{
    FeCaption(camp.cfg_file.c_str());
    // The table takes what is left under the button rows.
    const float below = ImGui::GetFrameHeightWithSpacing() * 4.0f;
    if (!ImGui::BeginTable("##ident", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y - below)))
        return;
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, form_col(18));
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, form_col(24));
    ImGui::TableHeadersRow();
    for (const KeyRow &r : rows_of("common"))
        if (draw_key_row(camp, "common", r, upper(r.key) != "NAME"))
            break; // the rows changed under us
    for (const char *block : {"strings", "speech"})
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("[%s]", block);
        ImGui::TableNextColumn();
        if (std::string(block) == "strings")
            ImGui::TextDisabled("language = text file; the first language line is the campaign's default");
        else
            ImGui::TextDisabled("language = speech folder");
        bool stop = false;
        for (const KeyRow &r : rows_of(block))
            if (!stop && draw_key_row(camp, block, r, true))
                stop = true;
    }
    ImGui::EndTable();

    std::vector<std::string> common_keys, langs;
    for (const struct NamedCommand *c = cmpgn_common_commands; c->name != nullptr; c++)
        common_keys.push_back(c->name);
    for (const struct NamedCommand *c = lang_type; c->name != nullptr; c++)
        langs.push_back(c->name);
    draw_add_key(camp, "common", common_keys);
    ImGui::SameLine();
    draw_add_key(camp, "strings", langs);
    ImGui::SameLine();
    draw_add_key(camp, "speech", langs);

    const bool shared = shared_location("CONFIGS_LOCATION") || shared_location("CREATURES_LOCATION");
    ImGui::BeginDisabled(!shared || dirty());
    if (FeButton("Give this campaign its own configuration", ImVec2(0, 0)))
    {
        FeOpenModal("Own configuration");
    }
    ImGui::EndDisabled();
    if (shared)
    {
        ImGui::SameLine();
        FeCaption(dirty() ? "Apply or Revert first." : "Its configuration folders are shared with other campaigns.");
    }
    const bool own_modal = FeBeginModal("Own configuration");
    if (own_modal)
    {
        ImGui::TextWrapped("Copy the shared configuration and creature folders to folders of this campaign's own and point "
                           "the campaign at them. The other campaigns keep the shared ones.");
        if (FeButton("Copy", ImVec2(110, 0)))
        {
            std::string err;
            if (apply_own_config(camp, &err))
                finish_write(camp, "The campaign now has its own configuration folders.");
            else
                s_cs.status = err;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (FeButton("Cancel", ImVec2(110, 0)))
            ImGui::CloseCurrentPopup();
    }
    FeEndModal(own_modal);
    draw_menu_order(camp);
}

// The map files of the campaign's levels folder that no list refers to.
std::vector<int64_t> unlisted_maps(const ContentCampaign &camp, const CampaignLevels &lv)
{
    std::vector<int64_t> out;
    std::error_code ec;
    if (camp.levels_dir.empty())
        return out;
    for (fs::directory_iterator it(camp.levels_dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (name.size() == 12 && name.compare(0, 3, "map") == 0 && name.compare(8, 4, ".slb") == 0)
        {
            const int64_t n = std::atoll(name.substr(3, 5).c_str());
            if (n > 0 && !lv.listed(n))
                out.push_back(n);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void select_number(int64_t n)
{
    for (size_t i = 0; i < s_cs.levels.size(); i++)
        if (s_cs.levels[i].number == n)
            s_cs.sel = (int64_t)i;
}

// Stages `lv` as the campaign's lists, plus a default entry for every level that has none.
void stage_levels(const ContentCampaign &camp, const CampaignLevels &lv)
{
    stage(camp, cfgc_levels_changes(s_cs.content, lv));
    for (int64_t n : lv.all())
        if (s_cs.view.find_section("map", n) == nullptr)
            stage(camp, cfgc_default_entry(n, (int64_t)lv.all().size() - 1, "Level " + std::to_string(n)));
}

const CfgContentSection *entry_of(int64_t n)
{
    return s_cs.view.find_section("map", n);
}

const CfgContentSection *entry_of_level(int64_t n)
{
    return s_cs.view.find_section("map", n);
}

std::string common_value_view(const char *key)
{
    if (const CfgContentSection *c = s_cs.view.find_section("common"))
        if (const std::string *v = c->last_value(key))
            return collapse(*v);
    return std::string();
}

std::string entry_value_of(int64_t n, const char *key)
{
    if (const CfgContentSection *e = s_cs.view.find_section("map", n))
        if (const std::string *v = e->last_value(key))
            return collapse(*v);
    return std::string();
}

std::string entry_value(int64_t n, const char *key)
{
    if (const CfgContentSection *e = entry_of(n))
        if (const std::string *v = e->last_value(key))
            return collapse(*v);
    return std::string();
}

// The images and window frames of the campaign's land folder (cached per folder).
void scan_land_folder()
{
    const std::string dir = common_value_view("LAND_LOCATION");
    if (dir == s_cs.land_scan_dir)
        return;
    s_cs.land_scan_dir = dir;
    s_cs.land_images.clear();
    s_cs.land_frames.clear();
    std::error_code ec;
    if (dir.empty())
        return;
    for (fs::directory_iterator it(content_root() + "/" + dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string stem = it->path().stem().string();
        const std::string ext = it->path().extension().string();
        if ((ext == ".raw" || ext == ".png") && std::find(s_cs.land_images.begin(), s_cs.land_images.end(), stem) == s_cs.land_images.end())
            s_cs.land_images.push_back(stem);
        else if (ext == ".dat" && stem.compare(0, 7, "viframe") == 0)
            s_cs.land_frames.push_back(stem);
    }
    std::sort(s_cs.land_images.begin(), s_cs.land_images.end());
    std::sort(s_cs.land_frames.begin(), s_cs.land_frames.end());
}

// The level's own land-view picture and window frame (LAND_VIEW = image frame); "(campaign default)" removes the key, so the
// level uses the start picture.
void draw_level_land_view(const ContentCampaign &camp, int64_t n)
{
    scan_land_folder();
    const std::string block = "map" + std::to_string(n);
    std::istringstream in(entry_value_of(n, "LAND_VIEW"));
    std::string image, frame;
    in >> image >> frame;
    auto pick = [&](const char *id, const std::vector<std::string> &choices, const std::string &current, float width) -> int64_t {
        std::vector<std::string> names = {"(campaign default)"};
        for (const std::string &c : choices)
            names.push_back(c);
        if (!current.empty() && std::find(choices.begin(), choices.end(), current) == choices.end())
            names.push_back(current + " (missing)");
        std::vector<const char *> ptrs;
        for (const std::string &x : names)
            ptrs.push_back(x.c_str());
        int64_t sel = 0;
        for (size_t i = 1; i < names.size(); i++)
            if (!current.empty() && (names[i] == current || names[i] == current + " (missing)"))
                sel = (int64_t)i;
        ImGui::SetNextItemWidth(width);
        int64_t before = sel;
        FeCombo(id, &sel, ptrs.data(), (int64_t)ptrs.size());
        return sel != before ? sel : -1;
    };
    FeCaption("Land view picture");
    ImGui::SameLine();
    std::vector<std::string> images = s_cs.land_images;
    const int64_t pi = pick("##lvimage", images, image, form_col(16));
    ImGui::SameLine();
    FeCaption("frame");
    ImGui::SameLine();
    const int64_t pf = pick("##lvframe", s_cs.land_frames, frame, form_col(14));
    if (pi >= 0 || pf >= 0)
    {
        std::string ni = image, nf = frame;
        if (pi == 0)
            ni.clear();
        else if (pi > 0 && (size_t)pi <= images.size())
            ni = images[(size_t)pi - 1];
        if (pf == 0)
            nf.clear();
        else if (pf > 0 && (size_t)pf <= s_cs.land_frames.size())
            nf = s_cs.land_frames[(size_t)pf - 1];
        if (ni.empty() && nf.empty())
            remove_pending(camp, block, "LAND_VIEW");
        else
        {
            // Both are needed; the missing one comes from the campaign's start picture.
            std::istringstream start(common_value_view("LAND_VIEW_START"));
            std::string si, sf;
            start >> si >> sf;
            set_pending(camp, block, "LAND_VIEW", (ni.empty() ? si : ni) + " " + (nf.empty() ? sf : nf));
        }
    }
}

// Speech played before and after the level (SPEECH = before after), files of the campaign's speech folder.
void draw_level_speech(const ContentCampaign &camp, int64_t n)
{
    const std::string block = "map" + std::to_string(n);
    std::istringstream in(entry_value_of(n, "SPEECH"));
    std::string before, after;
    in >> before >> after;
    std::string speech_dir;
    if (const CfgContentSection *sp = s_cs.view.find_section("speech"))
        if (!sp->fields.empty() && !sp->fields[0].values.empty())
            speech_dir = collapse(sp->fields[0].values.back());
    auto speech_input = [&](const char *label, const char *id, std::string &value) -> bool {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", value.c_str());
        FeCaption(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(form_col(14));
        const bool ch = ImGui::InputText(id, buf, sizeof(buf));
        if (ch)
            value = buf;
        if (!value.empty() && !speech_dir.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", fs::is_regular_file(content_root() + "/" + speech_dir + "/" + value) ? "found" : "not in the speech folder");
        }
        return ch;
    };
    const bool a = speech_input("Speech before", "##spbefore", before);
    ImGui::SameLine();
    const bool b = speech_input("after", "##spafter", after);
    if (a || b)
    {
        if (before.empty() && after.empty())
            remove_pending(camp, block, "SPEECH");
        else
            set_pending(camp, block, "SPEECH", before + " " + after);
    }
    if ((before.empty()) != (after.empty()))
    {
        ImGui::SameLine();
        FeCaption("Both file names are needed.");
    }
}

void draw_entry(const ContentCampaign &camp, int64_t n)
{
    const std::string block = "map" + std::to_string(n);
    FeCaption(("Level " + std::to_string(n) + " (the [map" + five(n) + "] entry: what the land-selection screen shows)").c_str());
    if (entry_of(n) == nullptr)
    {
        if (FeButton("Create entry", ImVec2(0, 0)))
            stage(camp, cfgc_default_entry(n, (int64_t)cfgc_read_levels(s_cs.view).all().size() - 1, "Level " + std::to_string(n)));
        return;
    }
    ImGui::PushID((int)n);
    auto text = [&](const char *label, const char *key, float width) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", entry_value(n, key).c_str());
        FeCaption(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(width);
        if (ImGui::InputText((std::string("##") + key).c_str(), buf, sizeof(buf)) && entry_value(n, key) != buf)
        {
            if (buf[0] == '\0' && std::string(key) != "NAME_TEXT")
                remove_pending(camp, block, key);
            else
                set_pending(camp, block, key, buf);
        }
    };
    text("Name", "NAME_TEXT", form_col(24));
    ImGui::SameLine();
    text("String id", "NAME_ID", form_col(6));
    ImGui::SameLine();
    text("Players", "PLAYERS", form_col(4));
    // Ensign kind: the flags the land view uses for the flag sprite.
    const char *legacy = (entry_of(n)->last_value("ENSIGN") == nullptr && entry_of(n)->last_value("OPTIONS") != nullptr) ? "OPTIONS" : "ENSIGN";
    std::vector<std::string> flags;
    {
        std::istringstream in(entry_value(n, legacy));
        for (std::string w; in >> w;)
            flags.push_back(upper(w));
    }
    FeCaption("Ensign");
    bool flags_changed = false;
    for (const char *f : {"TUTORIAL", "SINGLE", "BONUS", "FULL_MOON", "NEW_MOON", "COOP"})
    {
        ImGui::SameLine();
        const auto it = std::find(flags.begin(), flags.end(), f);
        bool on = it != flags.end();
        if (FeCheckbox(f, &on))
        {
            if (on)
                flags.push_back(f);
            else
                flags.erase(std::find(flags.begin(), flags.end(), f));
            flags_changed = true;
        }
    }
    if (flags_changed)
    {
        std::string v;
        for (const std::string &f : flags)
            v += (v.empty() ? "" : " ") + f;
        if (v.empty())
            remove_pending(camp, block, legacy);
        else
            set_pending(camp, block, legacy, v);
    }
    for (const char *key : {"ENSIGN_POS", "ENSIGN_ZOOM"})
    {
        std::istringstream in(entry_value(n, key));
        int x = 0, y = 0;
        in >> x >> y;
        FeCaption(key);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(form_col(8));
        bool ch = ImGui::InputInt((std::string("##x") + key).c_str(), &x, 0, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(form_col(8));
        ch = ImGui::InputInt((std::string("##y") + key).c_str(), &y, 0, 0) || ch;
        if (ch)
            set_pending(camp, block, key, std::to_string(x) + " " + std::to_string(y));
    }
    draw_level_land_view(camp, n);
    draw_level_speech(camp, n);
    ImGui::PopID();
}

void draw_levels(const ContentCampaign &camp)
{
    const CampaignLevels lv = cfgc_read_levels(s_cs.view);
    const std::vector<int64_t> unlisted = unlisted_maps(camp, lv);
    const LevelRow *row = (s_cs.sel >= 0 && s_cs.sel < (int64_t)s_cs.levels.size()) ? &s_cs.levels[(size_t)s_cs.sel] : nullptr;

    // Adding: a map file of the campaign's folder that no list has yet.
    std::vector<std::string> names;
    std::vector<const char *> ptrs;
    for (int64_t n : unlisted)
        names.push_back("Level " + std::to_string(n) + " (map" + five(n) + ")");
    for (const std::string &n : names)
        ptrs.push_back(n.c_str());
    ImGui::BeginDisabled(unlisted.empty());
    ImGui::SetNextItemWidth(form_col(20));
    s_cs.add_pick = std::min<int64_t>(s_cs.add_pick, std::max<int64_t>(0, (int64_t)unlisted.size() - 1));
    FeCombo("##addlevel", &s_cs.add_pick, ptrs.data(), (int64_t)ptrs.size());
    const int64_t pick = unlisted.empty() ? 0 : unlisted[(size_t)s_cs.add_pick];
    ImGui::SameLine();
    if (FeButton("Add as single", ImVec2(0, 0)))
    {
        CampaignLevels next = lv;
        cfgc_levels_add(next, CampList_Single, pick);
        stage_levels(camp, next);
        select_number(pick);
    }
    ImGui::SameLine();
    if (FeButton("Add as extra", ImVec2(0, 0)))
    {
        CampaignLevels next = lv;
        cfgc_levels_add(next, CampList_Extra, pick);
        stage_levels(camp, next);
        select_number(pick);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(row == nullptr || row->kind == CampList_Extra);
    if (FeButton("Bonus for selected", ImVec2(0, 0)))
    {
        CampaignLevels next = lv;
        cfgc_levels_set_bonus(next, row->index, pick);
        stage_levels(camp, next);
        select_number(pick);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (unlisted.empty())
    {
        ImGui::SameLine();
        FeCaption("Every map file in the campaign's folder is listed.");
    }

    // Working on the selected level.
    ImGui::BeginDisabled(row == nullptr || row->kind == CampList_Bonus);
    if (FeButton("Up", ImVec2(80, 0)))
    {
        CampaignLevels next = lv;
        const int64_t n = row->number;
        cfgc_levels_move(next, row->kind, row->index, -1);
        stage_levels(camp, next);
        select_number(n);
    }
    ImGui::SameLine();
    if (FeButton("Down", ImVec2(80, 0)))
    {
        CampaignLevels next = lv;
        const int64_t n = row->number;
        cfgc_levels_move(next, row->kind, row->index, +1);
        stage_levels(camp, next);
        select_number(n);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(row == nullptr);
    if (FeButton("Remove from campaign", ImVec2(0, 0)))
    {
        s_cs.confirm_remove = true;
        FeOpenModal("Remove level");
    }
    ImGui::EndDisabled();
    if (!s_cs.map_host)
    {
        ImGui::SameLine();
        ImGui::BeginDisabled(row == nullptr || !row->has_map || dirty());
        if (FeButton("Open in Map Editor", ImVec2(0, 0)))
        {
            frontend_request_map_editor_open(CampgnT_Campaign, camp.fname.c_str(), (LevelNumber)row->number, false);
            s_cs.open = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(row == nullptr || !row->has_map || dirty());
        if (FeButton("Play from this level", ImVec2(0, 0)))
        {
            // The game's own start path; when it ends the main menu comes back with this window open again.
            frontend_request_content_tool_play(CampgnT_Campaign, camp.fname.c_str(), (LevelNumber)row->number, ContentTool_Campaign);
            s_cs.open = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(dirty());
        if (FeButton("New map in this campaign", ImVec2(0, 0)))
        {
            editor_dialogs_set_default_campaign(camp.fname.c_str());
            frontend_request_map_editor_open(CampgnT_Campaign, camp.fname.c_str(), 0, true);
            s_cs.open = false;
        }
        ImGui::EndDisabled();
        if (dirty())
        {
            ImGui::SameLine();
            FeCaption("Apply or Revert first.");
        }
    }

    const bool remove_modal = FeBeginModal("Remove level");
    if (remove_modal && row != nullptr)
    {
        ImGui::TextWrapped("Take level %lld out of the campaign's lists. Its map files stay where they are.%s", (long long)row->number,
            row->kind == CampList_Single && lv.bonus[row->index] != 0 ? " Its bonus level is taken out too." : "");
        FeCheckbox("Also remove its [map] entry", &s_cs.remove_entry);
        if (FeButton("Remove", ImVec2(110, 0)))
        {
            CampaignLevels next = lv;
            std::vector<int64_t> gone;
            if (row->kind == CampList_Bonus)
            {
                gone.push_back(row->number);
                cfgc_levels_set_bonus(next, row->index, 0);
            }
            else
            {
                gone.push_back(row->number);
                if (row->kind == CampList_Single && lv.bonus[row->index] != 0)
                    gone.push_back(lv.bonus[row->index]);
                cfgc_levels_remove(next, row->kind, row->index);
            }
            stage(camp, cfgc_levels_changes(s_cs.content, next));
            if (s_cs.remove_entry)
                for (int64_t n : gone)
                {
                    ChangeSet cs;
                    cs.reset_section("map" + std::to_string(n));
                    stage(camp, cs);
                }
            s_cs.sel = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (FeButton("Cancel", ImVec2(110, 0)))
            ImGui::CloseCurrentPopup();
    }
    FeEndModal(remove_modal);

    if (s_cs.levels.empty())
    {
        FeCaption("This campaign lists no levels.");
        return;
    }
    const float below = row != nullptr ? ImGui::GetFrameHeightWithSpacing() * 9.0f : 0.0f;
    if (!ImGui::BeginTable("##levels", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y - below)))
        return;
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, form_col(8));
    ImGui::TableSetupColumn("List", ImGuiTableColumnFlags_WidthFixed, form_col(6));
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, form_col(6));
    ImGui::TableSetupColumn("Ensign", ImGuiTableColumnFlags_WidthFixed, form_col(9));
    ImGui::TableSetupColumn("Map files", ImGuiTableColumnFlags_WidthFixed, form_col(8));
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < s_cs.levels.size(); i++)
    {
        const LevelRow &r = s_cs.levels[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char label[48];
        snprintf(label, sizeof(label), "%lld###row%zu", (long long)r.number, i);
        if (ImGui::Selectable(label, s_cs.sel == (int64_t)i, ImGuiSelectableFlags_SpanAllColumns))
            s_cs.sel = (int64_t)i;
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.list.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.has_entry ? r.name.c_str() : "(no entry)");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.players.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.ensign.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.has_map ? "found" : "missing");
    }
    ImGui::EndTable();
    if (row != nullptr)
        draw_entry(camp, row->number);
}

// The land-view picture a level's ensign is placed on: the overview (LAND_VIEW_START), or for the zoom position the
// level's own LAND_VIEW when it has one.
std::string land_image_name(int64_t level, int64_t mode)
{
    std::string spec;
    if (mode == 1)
        if (const CfgContentSection *e = entry_of_level(level))
            if (const std::string *v = e->last_value("LAND_VIEW"))
                spec = *v;
    if (spec.empty())
        spec = common_value_view("LAND_VIEW_START");
    std::istringstream in(spec);
    std::string first;
    in >> first;
    return first;
}

// Loads the picture into the shared texture; the message says what is wrong when it cannot.
void ensure_land_texture(const std::string &base)
{
    if (s_cs.land_key == base)
        return;
    s_cs.land_key = base;
    s_cs.land_error.clear();
    s_cs.land_info.clear();
    LandviewImage img;
    bool png = false;
    if (!landview_load(base, img, s_cs.land_error, &png))
        return;
    if (s_cs.land_tex == nullptr)
        s_cs.land_tex = RendererCreateDynamicTexture(LANDVIEW_WIDTH, LANDVIEW_HEIGHT);
    if (s_cs.land_tex == nullptr)
    {
        s_cs.land_error = "no texture could be created";
        return;
    }
    const std::vector<uint8_t> rgba = landview_to_rgba(img);
    RendererUpdateDynamicTexture(s_cs.land_tex, rgba.data(), LANDVIEW_WIDTH, LANDVIEW_HEIGHT);
    s_cs.land_info = base + (png ? ".png" : ".raw + .pal");
}

void draw_land(const ContentCampaign &camp)
{
    if (s_cs.levels.empty())
    {
        FeCaption("This campaign lists no levels.");
        return;
    }
    const LevelRow *row = (s_cs.sel >= 0 && s_cs.sel < (int64_t)s_cs.levels.size()) ? &s_cs.levels[(size_t)s_cs.sel] : nullptr;
    static const char *const modes[] = {"Ensign position (overview)", "Ensign zoom position"};
    FeCaption("Place");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(form_col(20));
    FeCombo("##landmode", &s_cs.land_mode, modes, 2);
    ImGui::SameLine();
    FeCaption("Click or drag on the picture to set the selected level's position.");
    if (row != nullptr && row->has_entry)
        draw_level_land_view(camp, row->number);

    const float list_w = form_col(22);
    ImGui::BeginChild("##landlevels", ImVec2(list_w, 0.0f), true);
    for (size_t i = 0; i < s_cs.levels.size(); i++)
    {
        const LevelRow &r = s_cs.levels[i];
        char label[96];
        snprintf(label, sizeof(label), "%lld  %s###landrow%zu", (long long)r.number, r.has_entry ? r.name.c_str() : "(no entry)", i);
        if (ImGui::Selectable(label, s_cs.sel == (int64_t)i))
            s_cs.sel = (int64_t)i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();

    const char *key = s_cs.land_mode == 1 ? "ENSIGN_ZOOM" : "ENSIGN_POS";
    const std::string image = land_image_name(row != nullptr ? row->number : 0, s_cs.land_mode);
    std::string land_dir = common_value_view("LAND_LOCATION");
    if (image.empty() || land_dir.empty())
    {
        FeCaption("The campaign needs LAND_LOCATION and LAND_VIEW_START (Identity tab) to show its land view.");
        ImGui::EndGroup();
        return;
    }
    ensure_land_texture(content_root() + "/" + land_dir + "/" + image);
    if (!s_cs.land_error.empty())
    {
        FeCaption(("The picture cannot be shown: " + s_cs.land_error).c_str());
        ImGui::EndGroup();
        return;
    }
    FeCaption(s_cs.land_info.c_str());
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float scale = std::max(0.05f, std::min(avail.x / (float)LANDVIEW_WIDTH, avail.y / (float)LANDVIEW_HEIGHT));
    const ImVec2 size((float)LANDVIEW_WIDTH * scale, (float)LANDVIEW_HEIGHT * scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##landpicture", size);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddImage((ImTextureID)(intptr_t)s_cs.land_tex, origin, ImVec2(origin.x + size.x, origin.y + size.y));
    for (size_t i = 0; i < s_cs.levels.size(); i++)
    {
        const LevelRow &r = s_cs.levels[i];
        std::istringstream in(entry_value_of(r.number, key));
        int x = 0, y = 0;
        if (!(in >> x >> y))
            continue;
        const ImVec2 c(origin.x + (float)x * scale, origin.y + (float)y * scale);
        const bool selected = s_cs.sel == (int64_t)i;
        dl->AddCircleFilled(c, selected ? 11.0f : 8.0f, selected ? IM_COL32(255, 210, 60, 255) : IM_COL32(200, 40, 40, 255));
        dl->AddCircle(c, selected ? 11.0f : 8.0f, IM_COL32(0, 0, 0, 255), 0, 2.0f);
        char num[24];
        snprintf(num, sizeof(num), "%lld", (long long)r.number);
        dl->AddText(ImVec2(c.x + 12.0f, c.y - 8.0f), IM_COL32(255, 255, 255, 255), num);
    }
    if (row != nullptr && row->has_entry && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int x = std::max(0, std::min(LANDVIEW_WIDTH - 1, (int)((m.x - origin.x) / scale)));
        const int y = std::max(0, std::min(LANDVIEW_HEIGHT - 1, (int)((m.y - origin.y) / scale)));
        const std::string block = "map" + std::to_string(row->number);
        const std::string value = std::to_string(x) + " " + std::to_string(y);
        if (entry_value_of(row->number, key) != value)
            set_pending(camp, block, key, value);
    }
    else if (row != nullptr && !row->has_entry)
        FeCaption("This level has no entry yet: create it on the Levels page.");
    ImGui::EndGroup();
}

// Which editor handles a file ("" = the raw Config Files editor).
int tool_for_file(const RawFileEntry &f)
{
    if (f.creature_model || f.name == "creature.cfg")
        return ContentTool_Creature;
    if (f.name == "rules.cfg")
        return ContentTool_Rules;
    if (f.name == "trapdoor.cfg")
        return ContentTool_TrapDoor;
    if (f.name == "magic.cfg")
        return ContentTool_SpellAbility;
    if (f.name == "terrain.cfg")
        return ContentTool_Room;
    return ContentTool_ConfigFiles;
}

ConfigTarget campaign_target(const ContentCampaign &camp)
{
    return content_target_make(content_root() + "/fxdata", content_root() + "/creatrs", &camp, -1);
}

void rebuild_files(const ContentCampaign &camp)
{
    s_cs.files.clear();
    s_cs.files_stale = false;
    const ConfigTarget target = campaign_target(camp);
    for (const RawFileEntry &f : content_raw_list_files(target))
    {
        CampaignState::FileRow r;
        r.file = f;
        const std::string path = target.path_for(f.file_name, CfgLayer_Campaign, f.creature_model);
        std::error_code ec;
        if (!path.empty() && fs::is_regular_file(path, ec))
        {
            r.has = true;
            r.size = (int64_t)fs::file_size(path, ec);
            for (const CfgContentSection &sec : read_config_content(ConfigDocument::parse(slurp(path)), f.kind, true).sections)
                r.keys += sec.fields.size();
        }
        s_cs.files.push_back(std::move(r));
    }
}

// Creates the (empty) campaign-layer file: a comment line, so the other editors and the game find a file to work on.
void create_layer_file(const ContentCampaign &camp, const RawFileEntry &f)
{
    const ConfigTarget target = campaign_target(camp);
    const std::string path = target.path_for(f.file_name, CfgLayer_Campaign, f.creature_model);
    if (path.empty())
        return;
    WriteBatch batch;
    batch.put(path, "; " + f.file_name + " of the " + camp.name + " campaign: what is set here replaces the base values.\n");
    std::string err;
    if (batch.commit(&err))
        s_cs.files_stale = true;
    else
        s_cs.status = err;
}

void draw_files(const ContentCampaign &camp)
{
    if (s_cs.files_stale)
        rebuild_files(camp);
    // A campaign with no configuration / creature folder cannot have layer files: offer the folder.
    const struct { const char *key; const char *suffix; const std::string &dir; const char *what; } dirs[] = {
        {"CONFIGS_LOCATION", "cfg", camp.cfg_dir, "configuration"}, {"CREATURES_LOCATION", "crtr", camp.crtr_dir, "creature"}};
    for (const auto &d : dirs)
    {
        if (!d.dir.empty())
            continue;
        FeCaption((std::string("This campaign has no ") + d.what + " folder.").c_str());
        ImGui::SameLine();
        ImGui::PushID(d.key);
        ImGui::BeginDisabled(dirty());
        if (FeButton("Create one", ImVec2(0, 0)))
        {
            const std::string own = cfgc_own_location("campgns/x", camp.fname, d.suffix);
            std::error_code ec;
            fs::create_directories(content_root() + "/" + own, ec);
            set_pending(camp, "common", d.key, own);
            s_cs.status = "Folder created; Apply to point the campaign at it.";
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    FeCheckbox("Only the files this campaign has", &s_cs.files_only_own);
    const float below = ImGui::GetFrameHeightWithSpacing() * 1.5f;
    if (!ImGui::BeginTable("##files", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y - below)))
        return;
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("In this campaign", ImGuiTableColumnFlags_WidthFixed, form_col(9));
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, form_col(6));
    ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, form_col(5));
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, form_col(16));
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < s_cs.files.size(); i++)
    {
        const CampaignState::FileRow &r = s_cs.files[i];
        if (s_cs.files_only_own && !r.has)
            continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.file.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.has ? "yes" : "no");
        ImGui::TableNextColumn();
        if (r.has)
            ImGui::Text("%lld", (long long)r.size);
        ImGui::TableNextColumn();
        if (r.has)
            ImGui::Text("%zu", r.keys);
        ImGui::TableNextColumn();
        ImGui::PushID((int)i);
        const bool folder = !campaign_target(camp).path_for(r.file.file_name, CfgLayer_Campaign, r.file.creature_model).empty();
        if (!r.has)
        {
            ImGui::BeginDisabled(!folder);
            if (FeButton("Create", ImVec2(form_col(7), 0)))
                create_layer_file(camp, r.file);
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(dirty());
        if (FeButton("Open", ImVec2(form_col(7), 0)))
        {
            content_picker_set_last_campaign(camp.fname);
            content_tools_open_file(tool_for_file(r.file), r.file.name.c_str());
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// Where the campaign stands in the game's campaign menu, with move buttons (they write campgns/campgn_order.txt).
void draw_menu_order(const ContentCampaign &camp)
{
    size_t pos = 0, count = 0;
    if (!content_campaign_menu_position(camp.fname, &pos, &count))
        return;
    char info[96];
    snprintf(info, sizeof(info), "Place in the campaign menu: %zu of %zu", pos + 1, count);
    FeCaption(info);
    for (const int delta : {-1, +1})
    {
        ImGui::SameLine();
        ImGui::BeginDisabled(dirty() || (delta < 0 && pos == 0) || (delta > 0 && pos + 1 >= count));
        if (FeButton(delta < 0 ? "Move up" : "Move down", ImVec2(0, 0)))
        {
            std::string err;
            if (content_campaign_move_in_menu(camp.fname, delta, &err))
                finish_write(camp, "Menu order changed.");
            else
                s_cs.status = err;
        }
        ImGui::EndDisabled();
    }
}

void draw_check()
{
    if (s_cs.findings.empty())
    {
        FeCaption("No problems found.");
        return;
    }
    if (!ImGui::BeginTable("##check", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY))
        return;
    ImGui::TableSetupColumn("Line", ImGuiTableColumnFlags_WidthFixed, form_col(5));
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, form_col(7));
    ImGui::TableSetupColumn("Finding", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (const CfgDiagnostic &d : s_cs.findings)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (d.line > 0)
            ImGui::Text("%lld", (long long)d.line);
        ImGui::TableNextColumn();
        const bool error = d.severity >= CfgSev_Error;
        const bool warn = d.severity == CfgSev_Warning;
        ImGui::TextColored(error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : warn ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "%s", error ? "Error" : warn ? "Warning" : "Note");
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", d.message.c_str());
    }
    ImGui::EndTable();
}

void draw_new_campaign_modal()
{
    const bool modal = FeBeginModal("New campaign");
    if (modal)
    {
        FeHeading("New campaign");
        FeSeparator();
        FeCaption("Name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(form_col(26));
        if (ImGui::InputText("##newname", s_cs.new_name, sizeof(s_cs.new_name)) && !s_cs.new_id_touched)
            snprintf(s_cs.new_id, sizeof(s_cs.new_id), "%s", cfgc_campaign_id_from_name(s_cs.new_name).c_str());
        FeCaption("Folder / file name");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(form_col(20));
        if (ImGui::InputText("##newid", s_cs.new_id, sizeof(s_cs.new_id)))
            s_cs.new_id_touched = true;
        static const char *const colours[] = {"RED", "BLUE", "GREEN", "YELLOW", "WHITE", "PURPLE", "BLACK", "ORANGE"};
        FeCaption("Human player");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(form_col(10));
        FeCombo("##newhuman", &s_cs.new_human, colours, 8);
        FeCheckbox("Its own configuration and creature folders", &s_cs.new_own_config);
        const std::string id = s_cs.new_id;
        const bool valid_id = !id.empty() && id == cfgc_campaign_id_from_name(id);
        if (!valid_id)
            FeCaption("The file name may hold letters, digits and underscores only.");
        else if (!content_campaign_id_free(id))
            FeCaption("A campaign with that file name already exists.");
        else if (!s_cs.new_error.empty())
            FeCaption(s_cs.new_error.c_str());
        FeSeparator();
        ImGui::BeginDisabled(!valid_id || !content_campaign_id_free(id) || s_cs.new_name[0] == '\0');
        if (FeButton("Create", ImVec2(110, 0)))
        {
            if (content_campaign_create(s_cs.new_name, id, colours[s_cs.new_human], s_cs.new_own_config, &s_cs.new_error))
            {
                reselect(id + ".cfg");
                s_cs.status = "Campaign created; add levels on the Levels page.";
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (FeButton("Cancel", ImVec2(110, 0)))
            ImGui::CloseCurrentPopup();
    }
    FeEndModal(modal);
}

void draw_window()
{
    content_ui_begin_window("##ContentCampaign", s_cs.map_host);
    FeHeading("Campaign Editor");
    FeSeparator();

    if (s_cs.campaigns.empty())
    {
        FeCaption("The game knows no campaigns.");
    }
    else
    {
        FeCaption("Campaign");
        ImGui::SameLine();
        std::vector<std::string> names;
        std::vector<const char *> ptrs;
        for (const ContentCampaign &c : s_cs.campaigns)
            names.push_back(c.name);
        for (const std::string &n : names)
            ptrs.push_back(n.c_str());
        int64_t sel = s_cs.idx;
        ImGui::BeginDisabled(dirty());
        ImGui::SetNextItemWidth(form_col(28));
        if (FeCombo("##campaignpick", &sel, ptrs.data(), (int64_t)ptrs.size()) && sel != s_cs.idx)
        {
            s_cs.idx = sel;
            s_cs.loaded = false;
        }
        ImGui::SameLine();
        if (FeButton("Reload", ImVec2(110, 0)))
            s_cs.loaded = false;
        ImGui::SameLine();
        if (FeButton("New campaign...", ImVec2(0, 0)))
        {
            s_cs.new_name[0] = '\0';
            s_cs.new_id[0] = '\0';
            s_cs.new_id_touched = false;
            s_cs.new_error.clear();
            FeOpenModal("New campaign");
        }
        ImGui::EndDisabled();
        if (!s_cs.loaded)
            load();
    }
    draw_new_campaign_modal();
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty() || s_cs.campaigns.empty());
    if (FeButton("Apply", ImVec2(110, 0)))
    {
        const ContentCampaign camp = s_cs.campaigns[(size_t)s_cs.idx];
        std::string err;
        if (apply_pending(camp, &err))
            finish_write(camp, "Applied.");
        else
            s_cs.status = err;
    }
    ImGui::SameLine();
    if (FeButton("Revert", ImVec2(110, 0)))
        discard_pending(s_cs.campaigns[(size_t)s_cs.idx]);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (FeButton("Close", ImVec2(110, 0)))
    {
        if (dirty())
            s_cs.status = "Apply or Revert the changes first.";
        else
            s_cs.open = false;
    }

    size_t errors = 0, warnings = 0;
    for (const CfgDiagnostic &d : s_cs.findings)
    {
        errors += d.severity >= CfgSev_Error;
        warnings += d.severity == CfgSev_Warning;
    }
    ImGui::SameLine();
    if (!s_cs.status.empty())
        FeCaption(s_cs.status.c_str());
    else
    {
        char info[120];
        snprintf(info, sizeof(info), "%zu error(s), %zu warning(s)%s", errors, warnings, dirty() ? "; changes not applied" : "");
        FeCaption(info);
    }

    if (!s_cs.campaigns.empty() && s_cs.loaded)
    {
        const ContentCampaign &camp = s_cs.campaigns[(size_t)s_cs.idx];
        const bool tabs = FeBeginTabBar("##campaigntabs");
        if (FeTab("Identity"))
        {
            draw_identity(camp);
            FeEndTab();
        }
        if (FeTab("Levels"))
        {
            draw_levels(camp);
            FeEndTab();
        }
        if (FeTab("Config files"))
        {
            draw_files(camp);
            FeEndTab();
        }
        if (FeTab("Land view"))
        {
            draw_land(camp);
            FeEndTab();
        }
        char check[40];
        snprintf(check, sizeof(check), "Check (%zu)###checktab", errors + warnings);
        if (FeTab(check))
        {
            draw_check();
            FeEndTab();
        }
        FeEndTabBar(tabs);
    }
    ImGui::End();
}

} // namespace

void content_campaign_open(bool map_host)
{
    s_cs.open = true;
    s_cs.map_host = map_host;
    s_cs.everyone = content_list_campaigns();
    s_cs.campaigns.clear();
    for (const ContentCampaign &c : s_cs.everyone)
        if (!c.is_mappack)
            s_cs.campaigns.push_back(c);
    for (size_t i = 0; i < s_cs.campaigns.size(); i++)
        if (s_cs.campaigns[i].fname == s_cs.last_fname)
            s_cs.idx = (int64_t)i;
    s_cs.loaded = false;
    s_cs.status.clear();
}

void content_campaign_frame(void)
{
    if (s_cs.open)
        draw_window();
}

bool content_campaign_is_open(void)
{
    return s_cs.open;
}

bool content_campaign_test_select(const char *fname)
{
    rescan_lists();
    content_campaign_open(false);
    reselect(fname);
    if (s_cs.campaigns.empty() || s_cs.campaigns[(size_t)s_cs.idx].fname != fname)
        return false;
    load();
    return s_cs.loaded && !s_cs.doc.lines().empty();
}

void content_campaign_test_set(const char *block, const char *key, const char *value)
{
    set_pending(s_cs.campaigns[(size_t)s_cs.idx], block, key, value);
}

bool content_campaign_test_apply(void)
{
    const ContentCampaign camp = s_cs.campaigns[(size_t)s_cs.idx];
    std::string err;
    if (!apply_pending(camp, &err))
        return false;
    finish_write(camp, "Applied.");
    load();
    return true;
}

bool content_campaign_test_own_config(void)
{
    const ContentCampaign camp = s_cs.campaigns[(size_t)s_cs.idx];
    std::string err;
    if (!apply_own_config(camp, &err))
        return false;
    finish_write(camp, "own");
    load();
    return true;
}

bool content_campaign_test_level_op(const char *op, int64_t n, int64_t at)
{
    const ContentCampaign camp = s_cs.campaigns[(size_t)s_cs.idx];
    CampaignLevels lv = cfgc_read_levels(s_cs.view);
    const std::string what = op;
    if (what == "add_single")
        cfgc_levels_add(lv, CampList_Single, n);
    else if (what == "add_extra")
        cfgc_levels_add(lv, CampList_Extra, n);
    else if (what == "bonus")
    {
        const auto it = std::find(lv.single.begin(), lv.single.end(), at);
        if (it == lv.single.end())
            return false;
        cfgc_levels_set_bonus(lv, (size_t)(it - lv.single.begin()), n);
    }
    else if (what == "up")
    {
        const auto it = std::find(lv.single.begin(), lv.single.end(), n);
        if (it == lv.single.end())
            return false;
        cfgc_levels_move(lv, CampList_Single, (size_t)(it - lv.single.begin()), -1);
    }
    else if (what == "remove")
    {
        const auto it = std::find(lv.single.begin(), lv.single.end(), n);
        if (it == lv.single.end())
            return false;
        cfgc_levels_remove(lv, CampList_Single, (size_t)(it - lv.single.begin()));
        ChangeSet cs;
        cs.reset_section("map" + std::to_string(n));
        stage(camp, cfgc_levels_changes(s_cs.content, lv));
        stage(camp, cs);
        return true;
    }
    else
        return false;
    stage_levels(camp, lv);
    return true;
}

int64_t content_campaign_test_create_file(const char *name)
{
    const ContentCampaign camp = s_cs.campaigns[(size_t)s_cs.idx];
    rebuild_files(camp);
    bool found = false;
    for (const CampaignState::FileRow &r : s_cs.files)
        if (r.file.name == name)
        {
            found = true;
            if (!r.has)
                create_layer_file(camp, r.file);
        }
    if (!found)
        return -1;
    rebuild_files(camp);
    int64_t n = 0;
    for (const CampaignState::FileRow &r : s_cs.files)
        n += r.has;
    return n;
}
