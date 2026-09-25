/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_writer.h
 *     Header file for cfgc_writer.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.3 / §4.4 -- the writer class family. ConfigContentWriter is the parent
 *     (the map writer's role): it holds everything shared -- locating blocks,
 *     patching lines in place, no-op elimination against the lower layers,
 *     empty-block and empty-file rules, headers for created files, validation
 *     -- and the children supply the kind (schema, header, file name).
 *     A ChangeSet is the one vocabulary shared by editors, the raw editor,
 *     tests and JSON.
 * @par Comment:
 *     Existing files are never regenerated: only the lines a change touches are
 *     rewritten, keeping spacing, "=" or blank separator, key case, inline
 *     comments and line endings. Engine-decoupled (plan 10 §4.1).
 */
/******************************************************************************/
#ifndef DK_CFGC_WRITER_H
#define DK_CFGC_WRITER_H

#include <memory>
#include <string>
#include <vector>

#include "cfgc_document.h"
#include "cfgc_schema.h"
#include "cfgc_stack.h"
#include "cfgc_writebatch.h"

struct CfgChange
{
    enum Kind
    {
        Set,          // one key of one block to a value text ("20", "big med")
        Reset,        // remove the key from this layer (the lower layer's value shows through)
        ResetSection, // remove every key of the block from this layer
        ReplaceList   // the key's whole list of lines
    };
    Kind kind = Set;
    std::string section; // canonical block id: "trap2", "common", "game"
    std::string key;
    std::vector<std::string> values; // Set: the value text (words joined by a blank); ReplaceList: one per line
};

struct ChangeSet
{
    std::vector<CfgChange> changes;
    ChangeSet &set(const std::string &section, const std::string &key, const std::string &value);
    ChangeSet &reset(const std::string &section, const std::string &key);
    ChangeSet &reset_section(const std::string &section);
    ChangeSet &replace_list(const std::string &section, const std::string &key, const std::vector<std::string> &values);
};

struct ChangeResult
{
    bool ok = true;         // false: a change was malformed and nothing was applied
    size_t applied = 0;     // changes that altered the document
    size_t unchanged = 0;   // changes that were already true (or equal to the lower layer)
    std::vector<CfgDiagnostic> diagnostics;
};

class ConfigContentWriter
{
public:
    virtual ~ConfigContentWriter() {}

    // What the children supply.
    virtual const CfgFileSchema *schema() const = 0;
    virtual std::string header_line() const = 0; // first line of a file this writer creates
    // Turns a caller's value text into the text written; the default only trims.
    virtual std::string format_value(const CfgFieldSpec *spec, const std::string &text) const;

    // Patches `doc`. `lower` (may be null) holds the layers beneath the one being edited: a value equal
    // to what shows through is not written (an existing line for it is removed instead).
    ChangeResult apply(ConfigDocument &doc, const ChangeSet &changes, const ConfigStack *lower) const;

    // Text of a brand-new file for the changes (header, blank line, blocks); "" if nothing to write.
    std::string create(const ChangeSet &changes, const ConfigStack *lower) const;

    // Edits the file of `layer` for the target: reads it (or starts a new one), applies the changes and
    // queues the result in `batch` -- a put, or a delete when a generated file is left with no keys.
    // Returns false when nothing could be queued (no such layer for the target, malformed change).
    bool write(const ConfigTarget &target, CfgLayer layer, const std::string &file_name, const ChangeSet &changes,
        WriteBatch &batch, ChangeResult *result = nullptr, bool creature_model = false) const;

    // A file carrying the editor's header line was created by the editor (plan 10 §11.3/§11.4).
    static bool is_generated(const ConfigDocument &doc);
    static bool has_keys(const ConfigDocument &doc);
};

// Generic writer for the table-driven kinds: every section and key comes from the schema.
class TableConfigWriter : public ConfigContentWriter
{
public:
    TableConfigWriter(const CfgFileSchema *schema, std::string header) : schema_(schema), header_(std::move(header)) {}
    const CfgFileSchema *schema() const override { return schema_; }
    std::string header_line() const override { return header_; }

private:
    const CfgFileSchema *schema_;
    std::string header_;
};

// trapdoor.cfg: [trapN] and [doorN] blocks.
class TrapDoorConfigWriter : public TableConfigWriter
{
public:
    explicit TrapDoorConfigWriter(const ConfigSchema &schema);
};

// rules.cfg: named blocks plus the list blocks [research] and [sacrifices].
class RulesConfigWriter : public TableConfigWriter
{
public:
    explicit RulesConfigWriter(const ConfigSchema &schema);
};

// The writer for a schema kind: the dedicated child where there is one, else a TableConfigWriter with the
// standard header. Null if the kind has no schema.
std::unique_ptr<ConfigContentWriter> cfgc_make_writer(const ConfigSchema &schema, const std::string &kind);

#endif
