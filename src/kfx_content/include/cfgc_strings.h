/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_strings.h
 *     Header file for cfgc_strings.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/09-text-strings-editor.md §3 -- the language string files (`gtext_<lang>.dat`,
 *     a campaign's `text_<lang>.dat`, a level's `map%05d.<lang>.dat`) as data: a byte-preserving model of the
 *     NUL-separated entries, the reverse code-page conversion an editor needs, and the three-layer stack the game
 *     resolves a string id through.
 * @par Comment:
 *     Engine-decoupled (plan 10 §4.1). Format facts (config_strings.c fill_strings_list): a file is a run of
 *     NUL-terminated entries whose ordinal is the string id; an EMPTY entry means "inherit from the layer beneath";
 *     the loader rejects a file shorter than 16 bytes.
 */
/******************************************************************************/
#ifndef DK_CFGC_STRINGS_H
#define DK_CFGC_STRINGS_H

#include <cstdint>
#include <string>
#include <vector>

/** True for the languages that share the game's single-byte code page; Japanese, Chinese and Korean use multi-byte
 *  code pages the editor can show as raw bytes only (it never edits them). */
bool cfgc_lang_is_legacy_codepage(const std::string &lang_code);

class StringsFile
{
public:
    /** Never fails: any bytes parse. */
    static StringsFile parse(const std::string &bytes);
    /** Byte-identical to the parsed input while nothing was edited. */
    std::string serialize() const;

    size_t size() const { return entries_.size(); }
    /** The entry's raw bytes (in the language's code page); empty beyond the end. */
    const std::string &raw(size_t id) const;
    bool is_empty(size_t id) const { return raw(id).empty(); }
    /** Replaces an entry (growing the list with empty entries when `id` is beyond the end). */
    void set_raw(size_t id, const std::string &raw);
    /** Drops the empty entries at the end (ids are positions: internal ones are never dropped). */
    void trim_trailing_empty();
    /** The bytes to write: serialize() padded to the loader's 16-byte minimum. */
    std::string serialize_for_write() const;

private:
    std::vector<std::string> entries_;
    bool unterminated_tail_ = false; // the last entry has no NUL after it
};

/** Raw bytes -> UTF-8 (the game's code page; unmapped bytes become '?', as they do in the game). */
std::string cfgc_strings_decode(const std::string &raw);

struct StringsEncodeResult
{
    bool ok = true;
    std::string bytes;
    std::vector<uint64_t> unrepresentable; // codepoints the code page cannot hold (distinct, in order)
};
/** UTF-8 -> raw bytes. Characters the code page cannot hold are reported, and left out of `bytes`. */
StringsEncodeResult cfgc_strings_encode(const std::string &utf8);

enum StringLayer
{
    StringLayer_Base = 0,
    StringLayer_Campaign,
    StringLayer_Level,
    StringLayer_Count
};

/** The layers of one language: what the game resolves a string id through (level, then campaign, then base). */
struct StringsStack
{
    StringsFile layers[StringLayer_Count];
    bool present[StringLayer_Count] = {false, false, false};

    struct Effective
    {
        bool found = false;          // some layer has a non-empty entry
        StringLayer source = StringLayer_Base;
        std::string raw;
        bool has_beneath = false;    // a lower layer also has one
        StringLayer beneath_source = StringLayer_Base;
        std::string beneath_raw;
    };
    Effective effective(size_t id) const;
    /** One past the highest id any layer has an entry for. */
    size_t id_count() const;
};

#endif
