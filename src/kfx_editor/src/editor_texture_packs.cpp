/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_texture_packs.cpp
 *     See editor_texture_packs.h.
 */
#include "pre_inc.h"
#include "editor_texture_packs.h"
#include "engine_textures.h"
#include "config.h"
#include "frontgui_widgets.h"
#include <cstdio>
#include <string>
#include <vector>
#include "post_inc.h"

int editor_texture_pack_choices(LevelNumber lvnum, const EditorTexturePackChoice **out)
{
    static std::vector<std::string> labels;
    static std::vector<EditorTexturePackChoice> choices;
    labels.clear();
    choices.clear();
    labels.reserve(256);
    for (int i = 0; i < kTexturePackItemCount; i++)
        labels.push_back(kTexturePackItems[i]);
    const short fgroup = get_level_fgroup(lvnum);
    for (int id = kTexturePackItemCount; id < 256; id++)
    {
        if (texture_pack_available((unsigned long)id, lvnum, fgroup))
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d: Custom", id);
            labels.push_back(buf);
        }
        else
            labels.push_back(std::string());
    }
    for (int id = 0; id < 256; id++)
    {
        if (labels[(size_t)id].empty())
            continue;
        EditorTexturePackChoice c;
        c.id = id;
        c.label = labels[(size_t)id].c_str();
        choices.push_back(c);
    }
    *out = choices.data();
    return (int)choices.size();
}

bool editor_texture_pack_combo(const char *label, int *texture_id, LevelNumber lvnum)
{
    const EditorTexturePackChoice *choices = nullptr;
    const int count = editor_texture_pack_choices(lvnum, &choices);
    std::vector<const char *> items;
    int current = 0;
    for (int i = 0; i < count; i++)
    {
        items.push_back(choices[i].label);
        if (choices[i].id == *texture_id)
            current = i;
    }
    if (FeCombo(label, &current, items.data(), count))
    {
        *texture_id = choices[current].id;
        return true;
    }
    return false;
}
