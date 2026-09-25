/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_text.h
 *     Text routines header.
 * @par Purpose:
 *     Text conversion helper declarations.
 * @par Comment:
 *     Provides internal codepage to UTF-8 conversion helpers for text rendering.
 * @author   Tomasz Lis
 * @date     2026-06-13
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/*******************************************************************************/
#ifndef BFLIB_TEXT_H
#define BFLIB_TEXT_H

#include "bflib_basics.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

extern size_t convert_codepage_to_utf8_buffer(const char *src, size_t src_size, char *dst, size_t dst_size, uint8_t lang_id);
#define read_utf_8_codepoint(text, out_seq_len) read_utf_8_codepoint_f(text, out_seq_len,__func__)
extern uint64_t read_utf_8_codepoint_f(const char *text, size_t *out_seq_len, const char *func_name);
extern size_t encode_utf8_codepoint(uint64_t codepoint, char *dst, size_t dst_size);
/* The game's single-byte code page (every language except Japanese, Chinese and Korean shares it): the Unicode
 * codepoint a byte stands for, 0 for the NUL and for bytes the table leaves unmapped (which decode as '?'). */
extern uint64_t codepage_byte_to_unicode(unsigned char byte);
/* The reverse: the byte for a codepoint (1..255), or -1 when the code page cannot represent it. */
extern int64_t codepage_unicode_to_byte(uint64_t codepoint);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
