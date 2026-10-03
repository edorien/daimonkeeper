#pragma once

#include "globals.h"

#ifdef FUNCTESTING

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char TbBool;

/**
 * docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md, W5: the config content layer
 * (kfx_content's cfgc_*: ConfigStack, ConfigSchema, name registry, ConfigContentWriter) against the real
 * loaders in a running game.
 *
 * config_content_anchor: with the real configs loaded, the content layer's view (layers merged by
 * ConfigStack, values through the schema's ranges, names from the registry, block counts) must equal the
 * engine's live tables for trap, door, object, slab and room. Drift between the two implementations of the
 * format fails here.
 *
 * config_content_readback: before the level loads, writes a campaign-scope and a level-scope trapdoor.cfg
 * change with the writer, and checks the live values after the real loader ran (campaign-only key, key set
 * in both layers, untouched key). Restores the files afterwards.
 *
 * config_content_reset: writes a level-scope change, resets it through the writer (the generated file is
 * deleted), and checks the live value is the lower layers' again.
 */
TbBool ftest_config_content_anchor_init();
TbBool ftest_config_content_readback_init();
void ftest_config_content_readback_pre_start();
TbBool ftest_config_content_reset_init();
void ftest_config_content_reset_pre_start();

/**
 * config_content_tool_smoke: the Config Files window (plan 03 F3) opened from the map editor's host and
 * from the standalone host, drawn for a while inside a running editor session -- any ImGui misuse (unbalanced
 * Begin/End, bad IDs) would assert or crash here; also checks the host reports itself open.
 */
TbBool ftest_config_content_tool_smoke_init();

/**
 * config_content_rules_editor: the Rules editor's own session (content_struct.h) edits [game] PayDaySpeed at
 * level scope before the level loads; the running game must use it (plan 03 F4 acceptance). The level file is
 * removed afterwards.
 */
TbBool ftest_config_content_rules_editor_init();
void ftest_config_content_rules_editor_pre_start();

/**
 * config_content_scratch_level (plan 03 spike S6): the editor's Playtest runs a scratch copy of the map (level
 * number EDITOR_PLAYTEST_LEVEL_NUMBER) saved into a campaign's levels folder. This starts such a scratch level
 * (a copy of keeporig level 1 as map900002, with its own level-layer rules file) under the keeporig campaign
 * and checks the real loader applies both the campaign's configuration layer and the scratch level's own layer.
 * The scratch files are removed afterwards.
 */
TbBool ftest_config_content_scratch_level_init();

/**
 * config_content_trapdoor_editor: the Trap and Door editor's session edits a trap and a door at level scope
 * before the level loads (plan 05 T2 acceptance); the running game must use both values. The file is removed
 * afterwards. The smoke test also draws the editor (every tab, traps and doors) in both hosts.
 */
TbBool ftest_config_content_trapdoor_editor_init();
void ftest_config_content_trapdoor_editor_pre_start();

/**
 * config_content_spell_editor: the Spell and Ability editor's session edits a power's per-level Cost array and a
 * shot's Damage at level scope before the level loads (plan 06 S2 acceptance); the running game uses both.
 */
TbBool ftest_config_content_spell_editor_init();
void ftest_config_content_spell_editor_pre_start();

/**
 * config_content_creature_editor: the Creature editor's sessions edit a creature's Health (its model file), the
 * global [experience] Health percentage and an ability's Time (creature.cfg) at level scope before the level
 * loads (plan 04 C2 and plan 06 S4 acceptance); the running game uses all three, and the editor's experience
 * preview formula equals the engine's compute_creature_max_health() at level 4.
 */
TbBool ftest_config_content_creature_editor_init();
void ftest_config_content_creature_editor_pre_start();

/**
 * config_content_text_editor: the Text editor's session writes a level string file (map00001.eng.dat) with a new string
 * containing an accented letter and a line break, before the level loads (plan 09 X2 acceptance); the real loader's
 * get_string() returns it as UTF-8, and a base string the level does not touch is unchanged. The file is removed afterwards.
 */
TbBool ftest_config_content_text_editor_init();
void ftest_config_content_text_editor_pre_start();

/*
 * config_content_campaign_editor: the Campaign editor's edit and apply paths on a scratch campaign that shares keeporig's
 * configuration folders: a renamed campaign and a changed key show in the file and in the pack list without a restart, the
 * comments survive, "own configuration" copies the shared folders and repoints the campaign, and the shared campaign's own
 * file is not touched. (The scratch files are removed afterwards.)
 */
TbBool ftest_config_content_campaign_editor_init();
void ftest_config_content_campaign_editor_pre_start();

/*
 * config_content_landview_png: the game's land-view loader (load_map_and_window) takes a PNG named like the image (an indexed
 * 1280 x 960 PNG as it is: pixels and 6-bit palette) and still loads the .raw + .pal pair when there is no PNG.
 */
TbBool ftest_config_content_landview_png_init();
void ftest_config_content_landview_png_pre_start();

/**
 * config_content_room_editor: the Room editor's session edits a room's Cost, a slab's GoldHeld and a value of the
 * [block_health] table at level scope before the level loads (plan 07 R2/R3 acceptance); the running game uses all three.
 */
TbBool ftest_config_content_room_editor_init();
void ftest_config_content_room_editor_pre_start();
void ftest_config_content_scratch_level_pre_start();

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
