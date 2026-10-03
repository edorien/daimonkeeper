/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_writebatch.h
 *     Header file for cfgc_writebatch.cpp.
 * @par Purpose:
 *     docs/refactor/editor/fx-plans/10-config-content-model-and-writers.md
 *     §4.3 -- WriteBatch: all-or-nothing writes of several files. A campaign
 *     edit can touch a dozen files (a creature file, the campaign definition,
 *     the string files); either every one lands or the tree is left as it was.
 * @par Comment:
 *     Two phases. Stage: every new file is written next to its target as
 *     "<name>.kfxnew" and verified. Apply: for each operation the current file
 *     (if any) is moved to "<name>.kfxold", then the staged file is renamed
 *     into place. Any failure restores every backup and removes what was
 *     placed. Backups are deleted only once everything succeeded. A crash
 *     between the phases leaves ".kfxold" files, never a half-written target.
 *     Engine-decoupled (plan 10 §4.1): plain std::filesystem, no engine calls.
 */
/******************************************************************************/
#ifndef DK_CFGC_WRITEBATCH_H
#define DK_CFGC_WRITEBATCH_H

#include <string>
#include <vector>

class WriteBatch
{
public:
    enum OpKind { Op_Put, Op_Remove };
    struct Op
    {
        OpKind kind;
        std::string path;
        std::string bytes; // Op_Put only
    };

    // Write `bytes` to `path` (parent directories are created). A later put or
    // remove of the same path replaces the earlier operation.
    void put(const std::string &path, const std::string &bytes);
    // Delete `path` if it exists (a missing file is not an error).
    void remove(const std::string &path);

    const std::vector<Op> &ops() const { return ops_; }
    bool empty() const { return ops_.empty(); }

    // Runs the batch. On failure nothing has changed on disk and `error`
    // (when not null) says which file and why. An empty batch succeeds.
    bool commit(std::string *error = nullptr);

private:
    std::vector<Op> ops_;
};

#endif
