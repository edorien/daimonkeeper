#include "ftest_editor_script_commands.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "kfx_editor.h"
#include "editor_command_browser.h"
#include "editor_script.h"

#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ftest_editor_script_commands__variables
{
    int64_t unused;
};
struct ftest_editor_script_commands__variables ftest_editor_script_commands__vars = { 0 };

FTestActionResult ftest_editor_script_commands_action001__catalogue_and_insert(struct FTestActionArgs* const args);

TbBool ftest_editor_script_commands_init()
{
    ftest_append_action(ftest_editor_script_commands_action001__catalogue_and_insert, 20, &ftest_editor_script_commands__vars);
    return true;
}

FTestActionResult ftest_editor_script_commands_action001__catalogue_and_insert(struct FTestActionArgs* const args)
{
    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not mark the session active");
        return FTRs_Go_To_Next_Action;
    }

    int64_t count = editor_command_browser_command_count();
    if (count < 100)
    {
        FTEST_FAIL_TEST("Command catalogue holds only %" PRId64 " commands", (int64_t)(count));
        return FTRs_Go_To_Next_Action;
    }
    char other[1024];
    int64_t unclassified = editor_command_browser_unclassified(other, sizeof(other));
    JUSTLOG("Script command catalogue: %" PRId64 " commands, %" PRId64 " unclassified: %s", (int64_t)(count), (int64_t)(unclassified), other);
    if (unclassified > 0)
    {
        // Not fatal for users (they show under "Other"), but a new engine
        // command should get a group -- add it to kCommands in
        // editor_script_commands.cpp.
        FTEST_FAIL_TEST("Engine script commands with no group in the Script > Commands window: %s", other);
        return FTRs_Go_To_Next_Action;
    }

    // Closed script editor: appended to the session script text.
    editor_set_current_level_script_text("REM top\nIF(PLAYER0,MONEY>1)\n");
    if (editor_script_insert_command_at_cursor("WIN_GAME"))
    {
        FTEST_FAIL_TEST("insert reported the script editor as open");
        return FTRs_Go_To_Next_Action;
    }
    const char* text = editor_current_level_script_text();
    if (strcmp(text, "REM top\nIF(PLAYER0,MONEY>1)\n\tWIN_GAME\n") != 0)
    {
        FTEST_FAIL_TEST("Script text after insert is \"%s\"", text);
    }

    // Open editor: attaches the syntax-colouring language built from the live
    // command/creature/room/spell tables (must not crash) and inserts through
    // the widget path.
    editor_dialogs_open_script();
    if (!editor_script_is_open())
    {
        FTEST_FAIL_TEST("editor_dialogs_open_script() did not open the script editor");
        return FTRs_Go_To_Next_Action;
    }
    if (!editor_script_insert_command_at_cursor("LOSE_GAME"))
    {
        FTEST_FAIL_TEST("insert did not report the open script editor");
    }
    return FTRs_Go_To_Next_Action;
}

#endif
