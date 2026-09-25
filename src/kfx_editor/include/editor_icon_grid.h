/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_icon_grid.h
 *     Header file for editor_icon_grid.cpp.
 * @par Purpose:
 *     docs/refactor/editor/phase6/00-toolbox-icon-grids.md -- the toolbox's
 *     icon palette: a scrollable table of tiles drawn with the same cell
 *     primitive (fe_hud_cell) and icon sources (the panel sprites, plus the
 *     GUI_ICON_PACK PNG overrides) as the in-game sidebar's room / spell /
 *     trap grids, so the editor looks and behaves like the game. Items with
 *     no game icon (most terrain, decorations, specials) get a text tile.
 *     Internal to kfx_editor.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_ICON_GRID_H
#define DK_EDITOR_ICON_GRID_H

#include <stdint.h>
#ifdef __cplusplus

// How a tile's PNG-pack override is looked up (frontgui_ingame_icon_overrides.h).
enum EditorIconOverrideKind
{
    EIO_None = 0,
    EIO_ActiveInactive, // "<category>_<code>_active" (room_/power_/trap_)
    EIO_Single          // "<category>_<code>" (creature_icon)
};

struct EditorIconTile
{
    const char *id = "";                 // unique within the grid
    int64_t sprite = 0;                    // panel sprite index (medsym etc.); 0 = none
    EditorIconOverrideKind ov_kind = EIO_None;
    const char *ov_category = nullptr;   // e.g. "room", "power", "trap", "creature_icon"
    const char *ov_code = nullptr;       // code name for the override lookup
    void *thumb = nullptr;               // renderer texture (editor_thumbs.h): shown beside the label
    int64_t thumb_w = 0, thumb_h = 0;
    const char *label = nullptr;         // text tile (no sprite / fallback while a sprite loads)
    const char *tooltip = nullptr;
    bool selected = false;
};

// Opens the grid's scrolling child, `height` px tall, laid out in `cols`
// columns across the toolbox's fixed 240 px width. `cols` 5 suits icon tiles;
// 3 suits text tiles. Always pair with editor_icon_grid_end().
void editor_icon_grid_begin(const char *id, double height, int64_t cols);

// A full-width caption row that starts a new group of tiles.
void editor_icon_grid_heading(const char *text);

// Next tile in the flow. Returns true when clicked.
bool editor_icon_grid_tile(const EditorIconTile &tile);

void editor_icon_grid_end();

// "TORCH_WALL" -> "TORCH WALL": code names read badly in a small tile.
const char *editor_icon_grid_pretty(const char *code_name);

// Test-only: draw headings with ImGui's default font instead of the game's UI
// font (which loads from game data a unit test doesn't have).
void editor_icon_grid_test_use_default_font(bool use_default);

#endif // __cplusplus
#endif
