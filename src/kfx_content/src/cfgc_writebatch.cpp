/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_writebatch.cpp
 *     See cfgc_writebatch.h.
 */
#include "pre_inc.h"
#include "cfgc_writebatch.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include "post_inc.h"

namespace fs = std::filesystem;

/******************************************************************************/
namespace {

const char *const kNewSuffix = ".kfxnew";
const char *const kOldSuffix = ".kfxold";

struct Step
{
    fs::path target;
    fs::path staged;   // Put only
    fs::path backup;
    bool is_put = false;
    bool backed_up = false; // the original was moved to `backup`
    bool placed = false;    // the staged file now is the target
};

bool fail(std::string *error, const std::string &msg)
{
    if (error != nullptr)
        *error = msg;
    return false;
}

bool write_file(const fs::path &p, const std::string &bytes)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f)
        return false;
    f.write(bytes.data(), (std::streamsize)bytes.size());
    f.flush();
    return (bool)f;
}

void rollback(std::vector<Step> &steps)
{
    std::error_code ec;
    for (size_t i = steps.size(); i-- > 0;)
    {
        Step &s = steps[i];
        if (s.placed)
            fs::remove(s.target, ec);
        if (s.backed_up)
            fs::rename(s.backup, s.target, ec);
        if (s.is_put)
            fs::remove(s.staged, ec);
    }
}

void drop_op(std::vector<WriteBatch::Op> &ops, const std::string &path)
{
    for (size_t i = 0; i < ops.size(); i++)
        if (ops[i].path == path)
        {
            ops.erase(ops.begin() + (std::ptrdiff_t)i);
            return;
        }
}

} // namespace

void WriteBatch::put(const std::string &path, const std::string &bytes)
{
    drop_op(ops_, path);
    Op op;
    op.kind = Op_Put;
    op.path = path;
    op.bytes = bytes;
    ops_.push_back(std::move(op));
}

void WriteBatch::remove(const std::string &path)
{
    drop_op(ops_, path);
    Op op;
    op.kind = Op_Remove;
    op.path = path;
    ops_.push_back(std::move(op));
}

bool WriteBatch::commit(std::string *error)
{
    std::vector<Step> steps;
    std::vector<fs::path> made_dirs;
    std::error_code ec;

    // Phase 1: stage every new file beside its target.
    for (const Op &op : ops_)
    {
        Step s;
        s.target = fs::path(op.path);
        s.backup = fs::path(op.path + kOldSuffix);
        s.is_put = (op.kind == Op_Put);
        if (s.is_put)
        {
            s.staged = fs::path(op.path + kNewSuffix);
            const fs::path parent = s.target.parent_path();
            if (!parent.empty() && !fs::exists(parent, ec))
            {
                // Remember the first missing ancestor so a rollback can remove what was created.
                fs::path top = parent;
                while (top.has_parent_path() && !top.parent_path().empty() && !fs::exists(top.parent_path(), ec))
                    top = top.parent_path();
                fs::create_directories(parent, ec);
                if (ec)
                {
                    rollback(steps);
                    return fail(error, op.path + ": cannot create directory: " + ec.message());
                }
                made_dirs.push_back(top);
            }
            steps.push_back(s); // so a failed write still removes the partial staged file
            if (!write_file(s.staged, op.bytes))
            {
                rollback(steps);
                for (const fs::path &d : made_dirs)
                    fs::remove(d, ec);
                return fail(error, op.path + ": cannot write");
            }
            std::error_code sec;
            if (fs::file_size(s.staged, sec) != op.bytes.size() || sec)
            {
                rollback(steps);
                for (const fs::path &d : made_dirs)
                    fs::remove(d, ec);
                return fail(error, op.path + ": short write");
            }
        }
        else
        {
            steps.push_back(s);
        }
    }

    // Phase 2: back up each target, then move the staged file into place.
    for (Step &s : steps)
    {
        std::string why;
        // A leftover backup means an earlier run died mid-commit; it is stale by now.
        fs::remove(s.backup, ec);
        if (ec)
            why = "cannot clear stale backup: " + ec.message();
        else if (fs::exists(s.target, ec))
        {
            fs::rename(s.target, s.backup, ec);
            if (ec)
                why = "cannot back up: " + ec.message();
            else
                s.backed_up = true;
        }
        if (why.empty() && s.is_put)
        {
            fs::rename(s.staged, s.target, ec);
            if (ec)
                why = "cannot replace: " + ec.message();
            else
                s.placed = true;
        }
        if (!why.empty())
        {
            rollback(steps);
            for (const fs::path &d : made_dirs)
                fs::remove(d, ec);
            return fail(error, s.target.string() + ": " + why);
        }
    }

    // Success: the backups are no longer needed.
    for (Step &s : steps)
        if (s.backed_up)
            fs::remove(s.backup, ec);
    ops_.clear();
    return true;
}
