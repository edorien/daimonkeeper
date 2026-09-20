/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_sidecars.h
 *     Level files the editor does not save.
 * @par Purpose:
 *     KeeperFX loads more per-level files than MapContentWriter writes
 *     (map%05d.lua, .rules.cfg, .sounds.cfg, ...). A save in place leaves
 *     them alone, but Save As to a different folder or level number would
 *     silently leave them behind. This lists them so the dialog can warn.
 *     Plan: docs/refactor/editor/fx-plans/01-sidecar-files.md.
 */
#ifndef KFX_EDITOR_SIDECARS_H
#define KFX_EDITOR_SIDECARS_H

#include <string>
#include <vector>

/** True when the file extension (text after "map%05d.") is one the editor
 *  writes, regenerates or can safely drop; everything else is a sidecar. */
bool editor_sidecar_ext_is_handled(const std::string &ext);

/** Files named map<lvnum>.<ext> in `dir` whose extension is a sidecar,
 *  as bare file names, sorted. Empty when the folder is unreadable. */
std::vector<std::string> editor_find_sidecars(const char *dir, unsigned long lvnum);

/** True when saving to (dst_dir, dst_lvnum) is not a save in place. */
bool editor_save_is_relocation(const char *src_dir, unsigned long src_lvnum,
    const char *dst_dir, unsigned long dst_lvnum);

/** Removes every sidecar of (dir, lvnum). Returns the number removed. */
int editor_remove_sidecars(const char *dir, unsigned long lvnum);

/** Copies every sidecar of (src_dir, src_lvnum) to (dst_dir, dst_lvnum),
 *  renaming map<src>. to map<dst>. and overwriting. Returns the number copied. */
int editor_copy_sidecars(const char *src_dir, unsigned long src_lvnum,
    const char *dst_dir, unsigned long dst_lvnum);

#endif
