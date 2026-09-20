/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script_syntax.h
 *     Header file for editor_script_syntax.cpp.
 * @par Purpose:
 *     Syntax colouring for the script editor: a TextEditor language
 *     definition for Dungeon Keeper scripts, built from the engine's own
 *     command and value tables so it never drifts from what the parser
 *     accepts.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_EDITOR_SCRIPT_SYNTAX_H
#define DK_EDITOR_SCRIPT_SYNTAX_H

class TextEditor;

// Rebuilds the keyword / command / value sets from the engine's current
// tables (custom creatures, rooms and spells included) and attaches the
// language to `editor`; `enable` false removes colouring instead.
void editor_script_syntax_apply(TextEditor &editor, bool enable);

// Same for the Lua tab: the stock Lua language plus the KeeperFX API and
// engine names as known identifiers.
void editor_lua_syntax_apply(TextEditor &editor, bool enable);

#endif
