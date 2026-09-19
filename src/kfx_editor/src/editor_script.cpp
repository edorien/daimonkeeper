/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_script.cpp
 *     See editor_script.h. docs/refactor/editor/05-script-and-level-
 *     settings.md §4.1 -- vendors ImGuiColorTextEdit (deps/
 *     ImGuiColorTextEdit, MIT licensed) for the actual text-editing widget
 *     rather than building one from raw ImGui::InputTextMultiline() (no
 *     line numbers, no syntax highlighting, no find/replace -- confirmed no
 *     existing precedent for any of that anywhere in this codebase).
 * @par Comment:
 *     No syntax highlighting for the classic DK-script command format
 *     (IF/ENDIF/SET_GENERATE_SPEED/...) yet -- the library ships definitions
 *     for several real languages (C/C++/Lua/Python/...) but none for this
 *     one; TextEditor::Language is documented as extensible for a custom
 *     one, deliberately not attempted this slice (plain text is still a
 *     fully working editor, just without colour).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_script.h"
#include "editor_script_message.h"
#include "editor_script_commands.h"
#include "editor_script_syntax.h"
#include "kfx_editor.h"

#include "frontgui_widgets.h"

#include <imgui.h>
// TextEditor.h's inline constructors idiomatically shadow member names --
// fine in isolation (deps/ImGuiColorTextEdit's own .cpp doesn't link
// kfx_common_opts, so its own compilation never sees -Wshadow), but this
// header's inline code is recompiled here too, as part of this file, which
// DOES inherit kfx_common_opts's -Wshadow -Werror. Same
// push/ignore/pop-around-the-include precedent as frontend.cpp's own
// -Wmissing-field-initializers workaround, rather than weakening the flag
// for this whole file or touching vendored code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <TextEditor.h>
#include <cstring>
#pragma GCC diagnostic pop
#include "post_inc.h"

/******************************************************************************/
namespace {
    bool s_show_script_editor = false;
    TextEditor s_text_editor;
    // Syntax colouring on/off (editor_script_syntax.cpp). Kept across opens.
    bool s_colour_syntax = true;
} // namespace

void editor_dialogs_open_script(void)
{
    // Works around a real static-initialization-order fiasco: TextEditor's
    // own constructor calls SetPalette(defaultPalette), but defaultPalette
    // is itself a non-local static in TextEditor.cpp (a *different*
    // translation unit than this file's own s_text_editor) -- the C++
    // standard gives no guarantee that TextEditor.cpp's static initializer
    // for defaultPalette runs before this file's for s_text_editor. Found
    // live: it doesn't, at least not reliably -- s_text_editor.paletteBase
    // ended up a plain zero-filled array (every color, including text,
    // fully transparent), which explains everything about the "empty"/
    // "not accepting input" reports: the widget was working correctly the
    // whole time, just drawing every pixel with alpha=0. GetDarkPalette()
    // itself is safe (a function-local static, lazily and correctly
    // initialized on first call regardless of translation-unit order) --
    // calling it explicitly here, well after all static initialization has
    // finished, sidesteps the fiasco entirely without needing to patch the
    // vendored library. Idempotent, so doing it on every open is simplest.
    s_text_editor.SetPalette(TextEditor::GetDarkPalette());
    // Reset each time the window opens rather than keeping the widget's own
    // buffer as the source of truth across close/reopen -- editor_current_
    // level_script_text() (the session's own tracked copy, kept current by
    // this window's own Apply button) is the single source of truth, same
    // as every other Level Settings-style field.
    // Language sets are rebuilt from the engine tables on every open, so
    // custom creatures/rooms/spells added by the level's config are known.
    editor_script_syntax_apply(s_text_editor, s_colour_syntax);
    s_text_editor.SetText(editor_current_level_script_text());
    s_show_script_editor = true;
}

void editor_script_frame(void)
{
    if (!s_show_script_editor)
        return;

    // Plain ImGui::Begin(), not FeBeginModal() -- FeBeginModal() (every
    // other dialog in this codebase) is ImGuiWindowFlags_AlwaysAutoResize,
    // which can't be manually resized. A script can be long; the window
    // needs to be draggable-resizable, which is the default ImGui::Begin()
    // behaviour as long as neither NoResize nor AlwaysAutoResize is set.
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    bool open = s_show_script_editor;
    if (ImGui::Begin("Script Editor", &open, ImGuiWindowFlags_NoSavedSettings))
    {
        // Leaves room below the text area for the button row -- ImGui's own
        // "negative size means leave this many pixels free" convention,
        // same one TextEditor::Render()'s own size parameter follows.
        s_text_editor.Render("##EditorScriptText", ImVec2(0, -32));

        if (FeButton("Apply", ImVec2(140, 0)))
        {
            editor_set_current_level_script_text(s_text_editor.GetText().c_str());
            editor_mark_dirty();
        }
        ImGui::SameLine();
        if (FeButton("Close", ImVec2(140, 0)))
        {
            open = false;
        }
        ImGui::SameLine();
        if (FeCheckbox("Colour syntax", &s_colour_syntax))
            editor_script_syntax_apply(s_text_editor, s_colour_syntax);
    }
    ImGui::End();
    s_show_script_editor = open;
}
/******************************************************************************/

bool editor_script_insert_block_at_cursor(const char *block)
{
    if (s_show_script_editor)
    {
        std::string text = s_text_editor.GetText();
        size_t line_count = 1;
        for (char c : text)
            if (c == '\n')
                line_count++;
        size_t line = s_text_editor.GetMainCursorPosition().line;
        size_t safe = editor_script_safe_insert_line(text, line);
        if (safe < line_count)
        {
            // Through the widget so its own undo history keeps working.
            s_text_editor.ReplaceSectionText(TextEditor::DocPos(safe, 0), TextEditor::DocPos(safe, 0),
                std::string(block) + "\n");
        }
        else
        {
            // Past the last line (managed region is the tail of the script):
            // rebuild the text so the new line gets its own line break.
            s_text_editor.SetText(editor_script_insert_block(text, safe, block));
        }
        return true;
    }
    std::string text = editor_current_level_script_text();
    text = editor_script_insert_block(text, (size_t)-1, block);
    editor_set_current_level_script_text(text.c_str());
    editor_mark_dirty();
    return false;
}

extern "C" TbBool editor_script_is_open(void)
{
    return s_show_script_editor;
}

extern "C" TbBool editor_script_insert_command_at_cursor(const char *command)
{
    if (s_show_script_editor)
    {
        std::string text = s_text_editor.GetText();
        size_t cursor_line = s_text_editor.GetMainCursorPosition().line;
        ScriptInsertResult result = editor_script_insert_command(text, cursor_line, command);
        // One replace-everything edit so the widget's own undo treats the
        // insertion as a single step.
        size_t last_line = 0, last_len = 0;
        for (char c : text)
        {
            if (c == '\n')
            {
                last_line++;
                last_len = 0;
            }
            else
                last_len++;
        }
        s_text_editor.ReplaceSectionText(TextEditor::DocPos(0, 0), TextEditor::DocPos(last_line, last_len), result.text);
        // Cursor at the end of the inserted line, ready to fill in arguments.
        size_t line_start = 0;
        for (size_t l = 0; l < result.line; l++)
            line_start = result.text.find('\n', line_start) + 1;
        size_t line_end = result.text.find('\n', line_start);
        if (line_end == std::string::npos)
            line_end = result.text.size();
        size_t len = line_end - line_start;
        if (len > 0 && result.text[line_start + len - 1] == '\r')
            len--;
        s_text_editor.SetCursor(TextEditor::DocPos(result.line, len));
        return true;
    }
    std::string text = editor_current_level_script_text();
    // No cursor: append after the last non-blank line (so an open IF's
    // indentation carries over, and trailing blank lines stay trailing).
    size_t last_line = 0, line = 0;
    for (char c : text)
    {
        if (c == '\n')
        {
            line++;
        }
        else if (c != ' ' && c != '\t' && c != '\r')
        {
            last_line = line;
        }
    }
    ScriptInsertResult result = editor_script_insert_command(text, last_line, command);
    editor_set_current_level_script_text(result.text.c_str());
    editor_mark_dirty();
    return false;
}

extern "C" TbBool editor_script_insert_token_at_cursor(const char *token)
{
    if (!s_show_script_editor)
        return false;
    TextEditor::DocSelection sel = s_text_editor.GetMainCursorSelection();
    TextEditor::DocPos start = (sel.start <= sel.end) ? sel.start : sel.end;
    TextEditor::DocPos end = (sel.start <= sel.end) ? sel.end : sel.start;
    s_text_editor.ReplaceSectionText(start, end, token);
    s_text_editor.SetCursor(TextEditor::DocPos(start.line, start.index + std::strlen(token)));
    return true;
}
