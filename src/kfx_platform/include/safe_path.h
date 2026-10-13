/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file safe_path.h
 *     Which file names from content (a script, a campaign file, a save) may be opened.
 * @par Purpose:
 *     Content downloaded from someone else names files: a script loads a sound, a campaign says where its levels
 *     are. Such a name must stay inside the game's folders: not an absolute path, not a way up (".."), nothing that
 *     means something else to the operating system (a drive, a stream, a device). See docs/security-pass (S07).
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_SAFE_PATH_H
#define DK_SAFE_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Whether `path` is a plain relative path: not empty, under 1024 bytes, no control characters, relative (no leading
 * '/' or '\\', no drive or stream colon), no ".." component, no empty component ("a//b"), and no component that
 * names a Windows device (CON, NUL, COM1, LPT1, ... with or without an extension). '/' and '\\' both separate
 * components, on every platform, so that a path that is harmless here is harmless where it is moved to.
 * The path is only judged as text; whether it exists, or leads out through a link, is not looked at.
 */
int path_is_plain_relative(const char *path);

/**
 * Whether `path` can only name something below the folder it is joined to: no leading separator, no ".." component
 * (also as "..." or ".. ", which Windows reads the same), no drive or stream colon, no control character, no
 * component that names a Windows device, under 1024 bytes. Unlike path_is_plain_relative() it accepts what a
 * folder name or a search pattern can be: an empty path, a trailing separator, wildcards, "a//b".
 * This is the test the game's path builder applies to every part of a path that content can set (a mod's folder,
 * a campaign's locations, a file name from a config or a script).
 */
int path_cannot_escape(const char *path);

#ifdef __cplusplus
}
#endif

#endif
