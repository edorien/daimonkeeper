/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_picker.h
 *     docs/refactor/editor/fx-plans/03-content-editors-foundation.md §5 -- the target picker every
 *     content editor window starts with (campaign or map pack, level, layer), and the window frame
 *     they share so all of them look like the main menu's own screens.
 * @par Comment:
 *     Internal to kfx_editor. ImGui code (frontgui_widgets wrappers).
 */
#ifndef DK_CONTENT_PICKER_H
#define DK_CONTENT_PICKER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "content_target.h"

struct ContentPicker
{
    bool map_host = false; // opened from the map editor: the map is the level target
    std::vector<ContentCampaign> campaigns;
    int64_t campaign_idx = 0;
    int64_t level_idx = 0; // 0 = none, i+1 = campaign->levels[i]
    int64_t layer_sel = CfgLayer_Level;
    ConfigTarget target;
    std::string note;      // why a pick was refused
    // An editor whose layers are not the config files' (the text editor's string files) says which exist; when set it
    // replaces the config-directory test.
    std::function<bool(CfgLayer)> layer_filter;

    /** Lists the campaigns, restores the last-used one, resolves the target. `preferred`: the layer
     *  to start on when the target has it (the narrowest that exists, else Base). */
    void open(bool map_host_, CfgLayer preferred);
    void resolve();
    bool layer_available(CfgLayer l) const;
    const ContentCampaign *campaign() const;
    /** Campaign and level combos (or the map line in the map editor host). True when the target changed. */
    bool draw_target();
    /** Layer combo. True when the layer changed. */
    bool draw_layer();
    /** Changes whenever the target or layer changes. */
    std::string signature() const;
};

/** Begins the centred, undecorated content window (90% of the screen, no scrolling, translucent like the
 *  main menu screens; opaque over the map). Always pair with ImGui::End(). */
void content_ui_begin_window(const char *id, bool opaque);

/** Makes the editors opened next start on this campaign (its .cfg file name): the picker restores the last-used one. */
void content_picker_set_last_campaign(const std::string &campaign_fname);

#endif
