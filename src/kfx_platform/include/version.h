/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file version.h
 *     Project name, version, copyrights and debug level definitions.
 * @par Purpose:
 *     Header file for global names and defines used by resource compiler.
 * @par Comment:
 *     Can only contain commands which resource compiler can understand.
 * @author   Tomasz Lis
 * @date     08 Jan 2010 - 23 Jan 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KEEPERFX_VERSION_H
#define KEEPERFX_VERSION_H

#ifndef BFDEBUG_LEVEL
/* No longer affects logging (that is the LOG_LEVEL option, see globals.h's
 * *DBG macros); kept defined for code merged from upstream. */
#define BFDEBUG_LEVEL 0
#endif
#ifndef DEBUG_NETWORK_PACKETS
/* Network packets debugging. */
#define DEBUG_NETWORK_PACKETS 0
#endif
/* Version definitions */
#include "ver_defs.h"
/* Product identity. This game ships as dAImon Keeper, derived from KeeperFX
 * (it still needs the original Dungeon Keeper data files). PRODUCT_* is the
 * one place the name/slug/magic are spelled; KFX_COMPAT_* (ver_defs.h, from
 * build/make/version.mk) is the KeeperFX release whose content it supports. */
#define PRODUCT_NAME      "dAImon Keeper"
/* PRODUCT_SLUG ("daimonkeeper": executable, base config, log, save folder) comes
 * from build/make/version.mk through ver_defs.h, so the build tooling shares it. */
/* Program name, copyrights and file names */
#define PROGRAM_NAME      PRODUCT_NAME
#define PROGRAM_FULL_NAME PRODUCT_NAME
#define COMPANY_NAME      "dAImon Keeper contributors"
#define INTERNAL_NAME     PRODUCT_SLUG
#define LEGAL_COPYRIGHT   "GPLv2 or later; based on KeeperFX"
#define LEGAL_TRADEMARKS  "DK is a trademark of Electronic Arts"
#define FILE_VERSION VER_STRING
#define FILE_DESCRIPTION PROGRAM_NAME
/* One literal, not INTERNAL_NAME".exe": in a .rc file "" inside a string is an
 * escaped quote, so that concatenation read daimonkeeper".exe" in the PE info. */
#define ORIGINAL_FILENAME PRODUCT_EXE_NAME
#define PRODUCT_VERSION    VER_STRING
#define DEFAULT_LOG_FILENAME INTERNAL_NAME".log"

/* 'D','M','K','R' in file/wire byte order (little-endian uint32). Identifies
 * this game -- as opposed to KeeperFX -- in net/save/replay headers. */
#define PRODUCT_MAGIC     0x524B4D44u
/* "dAImon Keeper 1.0.0 <em dash U+2014> KFX 1.4", UTF-8: ImGui text and logs. */
#define PRODUCT_VERSION_LABEL PRODUCT_NAME " " VER_SHORT_STRING " \xE2\x80\x94" " " KFX_COMPAT_STRING
/* Same with a plain hyphen, for the legacy bitmap fonts (no em dash glyph). */
#define PRODUCT_VERSION_LABEL_ASCII PRODUCT_NAME " " VER_SHORT_STRING " - " KFX_COMPAT_STRING

#endif /*KEEPERFX_VERSION_H*/
/******************************************************************************/
