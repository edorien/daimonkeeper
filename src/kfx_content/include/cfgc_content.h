/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_content.h
 *     Header file for cfgc_content.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.2 -- ConfigContent: the structured, sparse view of one configuration
 *     file at one layer. Ordered sections, each with ordered fields, each field
 *     a key with one value text per occurrence (keys may repeat). This is what
 *     an editor or a JSON tool reads and proposes; the lossless text stays in
 *     ConfigDocument.
 * @par Comment:
 *     Values are the raw value text with any inline ";" comment removed. Turning
 *     them into numbers, names or flag lists is the schema's job (W2), not
 *     this layer's. Engine-decoupled (plan 10 §4.1).
 */
/******************************************************************************/
#ifndef DK_CFGC_CONTENT_H
#define DK_CFGC_CONTENT_H

#include <cstdint>
#include <string>
#include <vector>

#include "cfgc_document.h"

struct CfgField
{
    std::string key; // spelling of the first occurrence
    std::vector<std::string> values; // one entry per occurrence, in file order
    bool operator==(const CfgField &o) const { return key == o.key && values == o.values; }
};

struct CfgContentSection
{
    std::string name;     // "trap12", "common", "creature"
    std::string basename; // name without its trailing number: "trap"; the whole name if it has none
    int64_t index = -1;   // trailing number, or -1 for a named section
    std::vector<CfgField> fields;

    // Keys match case-insensitively, like the loader; nullptr if absent.
    const CfgField *find_field(const std::string &key) const;
    // The value the loader keeps for a scalar key (the last occurrence), or nullptr.
    const std::string *last_value(const std::string &key) const;
    bool operator==(const CfgContentSection &o) const
    {
        return name == o.name && fields == o.fields;
    }
};

struct ConfigContent
{
    std::string kind; // caller's label for the file type ("trapdoor", "rules", ...)
    bool partial = false; // a campaign/level layer rather than a complete base file
    // Blocks in file order. A block name repeated in the file stays repeated here.
    std::vector<CfgContentSection> sections;

    // First section called `name` (case-sensitive, like the loader), or nullptr.
    const CfgContentSection *find_section(const std::string &name) const;
    // First section with this basename and index, or nullptr.
    const CfgContentSection *find_section(const std::string &basename, int64_t index) const;

    bool operator==(const ConfigContent &o) const
    {
        return kind == o.kind && partial == o.partial && sections == o.sections;
    }
};

// Reads every "[section]" block of the document. Lines before the first header
// are ignored, as the loader ignores them. Comments, blanks and junk lines
// contribute nothing; keys without a value give an empty value text.
ConfigContent read_config_content(const ConfigDocument &doc, const std::string &kind, bool partial);

// Splits "trap12" into ("trap", 12); a name without trailing digits is returned
// whole with index -1.
void cfgc_split_section_name(const std::string &name, std::string &basename, int64_t &index);

#endif
