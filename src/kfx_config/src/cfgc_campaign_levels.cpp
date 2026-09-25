/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_campaign_levels.cpp
 *     See cfgc_campaign_levels.h.
 */
#include "pre_inc.h"
#include "cfgc_campaign_levels.h"
#include "cfgc_campaign_check.h"
#include "cfgc_schema.h"

#include <algorithm>
#include "post_inc.h"

/******************************************************************************/
bool CampaignLevels::listed(int64_t n) const
{
    for (const std::vector<int64_t> *v : {&single, &bonus, &extra})
        if (std::find(v->begin(), v->end(), n) != v->end() && n != 0)
            return true;
    return false;
}

std::vector<int64_t> CampaignLevels::all() const
{
    std::vector<int64_t> out;
    for (const std::vector<int64_t> *v : {&single, &bonus, &extra})
        for (int64_t n : *v)
            if (n != 0)
                out.push_back(n);
    return out;
}

CampaignLevels cfgc_read_levels(const ConfigContent &content)
{
    CampaignLevels lv;
    if (const CfgContentSection *c = content.find_section("common"))
    {
        auto list = [&](const char *key) {
            const std::string *v = c->last_value(key);
            return v != nullptr ? cfgc_parse_level_list(*v) : std::vector<int64_t>();
        };
        lv.single = list("SINGLE_LEVELS");
        lv.bonus = list("BONUS_LEVELS");
        lv.extra = list("EXTRA_LEVELS");
    }
    lv.bonus.resize(lv.single.size(), 0);
    return lv;
}

void cfgc_levels_add(CampaignLevels &lv, CampaignListKind kind, int64_t n)
{
    if (n <= 0 || lv.listed(n))
        return;
    if (kind == CampList_Single)
    {
        lv.single.push_back(n);
        lv.bonus.push_back(0);
    }
    else if (kind == CampList_Extra)
        lv.extra.push_back(n);
}

void cfgc_levels_set_bonus(CampaignLevels &lv, size_t index, int64_t bonus)
{
    if (index >= lv.single.size() || (bonus != 0 && lv.listed(bonus)))
        return;
    lv.bonus[index] = std::max<int64_t>(0, bonus);
}

void cfgc_levels_move(CampaignLevels &lv, CampaignListKind kind, size_t index, int delta)
{
    const int64_t to = (int64_t)index + delta;
    if (kind == CampList_Extra)
    {
        if (index < lv.extra.size() && to >= 0 && to < (int64_t)lv.extra.size())
            std::swap(lv.extra[index], lv.extra[(size_t)to]);
    }
    else if (index < lv.single.size() && to >= 0 && to < (int64_t)lv.single.size())
    {
        std::swap(lv.single[index], lv.single[(size_t)to]);
        std::swap(lv.bonus[index], lv.bonus[(size_t)to]);
    }
}

void cfgc_levels_remove(CampaignLevels &lv, CampaignListKind kind, size_t index)
{
    if (kind == CampList_Extra)
    {
        if (index < lv.extra.size())
            lv.extra.erase(lv.extra.begin() + (long)index);
    }
    else if (index < lv.single.size())
    {
        lv.single.erase(lv.single.begin() + (long)index);
        lv.bonus.erase(lv.bonus.begin() + (long)index);
    }
}

int64_t cfgc_next_level_number(const CampaignLevels &lv, CampaignListKind kind)
{
    int64_t highest = 0;
    if (kind == CampList_Single)
    {
        for (int64_t n : lv.single)
            highest = std::max(highest, n);
        return highest + 1;
    }
    highest = 99;
    for (const std::vector<int64_t> *v : {&lv.bonus, &lv.extra})
        for (int64_t n : *v)
            highest = std::max(highest, n);
    return highest + 1;
}

size_t cfgc_list_pitch(const std::string &value)
{
    // Start of each word; the pitch is the distance between the first two.
    std::vector<size_t> starts;
    for (size_t i = 0; i < value.size(); i++)
        if (value[i] != ' ' && value[i] != '\t' && (i == 0 || value[i - 1] == ' ' || value[i - 1] == '\t'))
            starts.push_back(i);
    return starts.size() >= 2 ? starts[1] - starts[0] : 0;
}

std::string cfgc_format_level_list(const std::vector<int64_t> &numbers, size_t pitch)
{
    size_t widest = 1;
    for (int64_t n : numbers)
        widest = std::max(widest, std::to_string(n).size());
    if (pitch < widest + 1)
        pitch = widest + 1;
    std::string out;
    for (size_t i = 0; i < numbers.size(); i++)
    {
        std::string w = std::to_string(numbers[i]);
        if (i + 1 < numbers.size())
            w.resize(pitch, ' ');
        out += w;
    }
    return out;
}

ChangeSet cfgc_levels_changes(const ConfigContent &current, const CampaignLevels &lv)
{
    std::string existing;
    if (const CfgContentSection *c = current.find_section("common"))
        for (const char *k : {"SINGLE_LEVELS", "BONUS_LEVELS"})
            if (existing.empty() || cfgc_list_pitch(existing) == 0)
                if (const std::string *v = c->last_value(k))
                    existing = *v;
    // The content reader keeps the value as written (spacing included).
    size_t pitch = cfgc_list_pitch(existing);
    // The single and bonus lists are parallel: give them one column width so they line up.
    for (const std::vector<int64_t> *v : {&lv.single, &lv.bonus})
        for (int64_t n : *v)
            pitch = std::max(pitch, std::to_string(n).size() + 1);
    ChangeSet cs;
    cs.set("common", "SINGLE_LEVELS", cfgc_format_level_list(lv.single, pitch));
    cs.set("common", "BONUS_LEVELS", cfgc_format_level_list(lv.bonus, pitch));
    if (lv.extra.empty())
        cs.reset("common", "EXTRA_LEVELS");
    else
        cs.set("common", "EXTRA_LEVELS", cfgc_format_level_list(lv.extra, pitch));
    return cs;
}

ChangeSet cfgc_default_entry(int64_t level, int64_t slot, const std::string &name)
{
    // Spread along the overview picture (1280 x 960) in rows of six, so a new level is selectable at once.
    const int64_t x = 160 + (slot % 6) * 190;
    const int64_t y = 160 + ((slot / 6) % 4) * 190;
    const std::string id = "map" + std::to_string(level);
    ChangeSet cs;
    cs.set(id, "NAME_TEXT", name);
    cs.set(id, "ENSIGN_POS", std::to_string(x) + " " + std::to_string(y));
    cs.set(id, "ENSIGN_ZOOM", std::to_string(x) + " " + std::to_string(y));
    cs.set(id, "PLAYERS", "1");
    return cs;
}

std::string cfgc_campaign_add_level(const std::string &file_text, int64_t n, CampaignListKind kind, const std::string &name, bool *changed)
{
    ConfigDocument doc = ConfigDocument::parse(file_text);
    const ConfigContent content = read_config_content(doc, "campaign", false);
    CampaignLevels lv = cfgc_read_levels(content);
    const bool was_listed = lv.listed(n);
    ChangeSet cs;
    if (!was_listed)
    {
        cfgc_levels_add(lv, kind == CampList_Extra ? CampList_Extra : CampList_Single, n);
        cs = cfgc_levels_changes(content, lv);
    }
    if (content.find_section("map", n) == nullptr)
    {
        const ChangeSet entry = cfgc_default_entry(n, (int64_t)lv.all().size() - 1, name);
        cs.changes.insert(cs.changes.end(), entry.changes.begin(), entry.changes.end());
    }
    cfgc_make_writer(build_engine_schema(), "campaign")->apply(doc, cs, nullptr);
    const std::string out = doc.serialize();
    if (changed != nullptr)
        *changed = out != file_text;
    return out;
}
