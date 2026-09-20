/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_document.h
 *     Header file for cfgc_document.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.2 -- the lossless, line-oriented model of one text configuration
 *     file (`.cfg`). Raw lines are kept verbatim (line endings, BOM, `^Z`,
 *     banner "sections", comments, lines with no `=`), with an index over
 *     sections and key lines on top. Parsing never fails, and a document that
 *     was not edited serialises back byte for byte.
 * @par Comment:
 *     Engine-decoupled on purpose (plan 10 §4.1): no kfx_config_state, no
 *     loader calls, no path groups. Everything is explicit.
 */
/******************************************************************************/
#ifndef DK_CFGC_DOCUMENT_H
#define DK_CFGC_DOCUMENT_H

#include <cstddef>
#include <string>
#include <vector>

enum CfgLineKind
{
    CfgLine_Blank = 0,
    CfgLine_Comment, // first non-blank character is ';'
    CfgLine_Section, // "[name]"
    CfgLine_Key,     // "Key = value" or "Key value"
    CfgLine_Other    // anything else (junk the loader ignores, a lone ^Z, ...)
};

struct CfgLine
{
    CfgLineKind kind = CfgLine_Blank;
    std::string text;  // the line without its end-of-line
    std::string eol;   // "", "\n" or "\r\n"
    // CfgLine_Section: the text between the brackets. CfgLine_Key: the key.
    std::string name;
    // CfgLine_Key only: the value text after the separator, trailing blanks removed.
    std::string value;
    bool has_equals = false;
};

// A "[name]" block: its header line and the lines up to (not including) the
// next header. Lines before the first header belong to the preamble block,
// whose name is empty and whose header_line is -1.
struct CfgSection
{
    std::string name;
    long header_line = -1;
    long first_line = 0; // first line after the header
    long end_line = 0;   // one past the last line of the block
};

class ConfigDocument
{
public:
    // Never fails: any byte sequence parses.
    static ConfigDocument parse(const std::string &bytes);

    // Byte-identical to the parsed input while the document is unedited.
    std::string serialize() const;

    const std::vector<CfgLine> &lines() const { return lines_; }
    const std::vector<CfgSection> &sections() const { return sections_; }

    // Index of the first block called `name` (case-sensitive, like the
    // loader's block matching), or -1.
    long find_section(const std::string &name) const;

    // Line indices of the keys in a block, in file order.
    std::vector<long> key_lines(long section_index) const;

    // Predominant line ending ("\r\n" or "\n"); "\n" for an empty document.
    std::string dominant_eol() const;

private:
    void rebuild_index();

    std::vector<CfgLine> lines_;
    std::vector<CfgSection> sections_;
};

#endif
