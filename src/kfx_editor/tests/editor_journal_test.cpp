// docs/refactor/editor/09-toolbox-remainder.md's "Broader test coverage"
// backlog item -- Catch2 coverage for editor_journal.cpp's undo/redo
// journal: inverse-arg capture (record_placement/record_rect_terrain),
// describe_undo/describe_redo's per-pcktype labels, do_undo/do_redo's
// packet dispatch (including the four ambient-position placement verbs'
// Redo-counterpart remap), and stack_push()'s overflow/eviction behavior --
// a direct regression test for the memmove-on-a-vector-owning-struct bug
// fixed earlier in this same effort (EditorJournalEntry owns a std::vector,
// so evicting the oldest entry must move-construct each surviving slot,
// not raw-copy bytes between them).
//
// record_placement()/record_rect_terrain() both gate on editor_is_active(),
// which has no other exported override and needs a genuinely loaded
// level/player via the real editor_open() -- impractical for an isolated
// unit test. editor_journal_test_force_active() (added alongside this
// file) is the test-only seam that opens that gate directly instead
// (docs/refactor/testing/stage-02-testability-and-fakes.md's "Pattern A",
// adapted to a hidden bool behind a getter rather than an exported struct
// field).
#include <catch2/catch_test_macros.hpp>

#include "editor_journal.h"
#include "kfx_editor.h"
#include "packet_data.h"

#include <string>

namespace {

TbBool contains(const char *haystack, const char *needle)
{
    return std::string(haystack).find(needle) != std::string::npos;
}

// Forces editor_is_active()'s gate open for the test body, and always
// leaves the journal reset and the gate closed again on the way out --
// regardless of which SECTION ran or whether an assertion failed midway.
struct JournalTestActive {
    JournalTestActive() {
        editor_journal_reset();
        editor_journal_test_force_active(true);
    }
    ~JournalTestActive() {
        editor_journal_test_force_active(false);
        editor_journal_reset();
    }
};

} // namespace

TEST_CASE_METHOD(JournalTestActive, "editor_journal_reset clears both stacks", "[kfx_editor][journal]")
{
    editor_journal_record_placement(1, PckA_EditorPlaceObject, 10, 20, 3, 0, 0, 0);
    REQUIRE(editor_journal_undo_count() == 1);

    editor_journal_reset();

    CHECK(editor_journal_undo_count() == 0);
    CHECK(editor_journal_redo_count() == 0);
}

TEST_CASE_METHOD(JournalTestActive, "record_placement/record_rect_terrain no-op outside an active session", "[kfx_editor][journal]")
{
    editor_journal_test_force_active(false); // simulate a real, inactive session

    editor_journal_record_placement(1, PckA_EditorPlaceObject, 10, 20, 3, 0, 0, 0);
    CHECK(editor_journal_undo_count() == 0);

    struct EditorRectSlabSnapshot before[1] = {{0, 0}};
    editor_journal_record_rect_terrain(PckA_EditorPlaceTerrainRect, 0, 0, 0, 0, 11, 0, before, 1);
    CHECK(editor_journal_undo_count() == 0);
}

TEST_CASE_METHOD(JournalTestActive, "record_placement pushes onto the undo stack, described per pcktype", "[kfx_editor][journal]")
{
    SECTION("creature") {
        editor_journal_record_placement(5, PckA_CheatMakeCreature, /*model*/1, /*owner*/0, 0, 0, 100, 200);
        REQUIRE(editor_journal_undo_count() == 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Creature:"));
    }
    SECTION("object") {
        editor_journal_record_placement(6, PckA_EditorPlaceObject, 100, 200, /*model*/3, 0, 0, 0);
        REQUIRE(editor_journal_undo_count() == 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Object:"));
    }
    SECTION("trap") {
        editor_journal_record_placement(7, PckA_EditorPlaceTrap, /*model*/1, 0, 0, 0, 100, 200);
        REQUIRE(editor_journal_undo_count() == 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Trap:"));
    }
    SECTION("door") {
        editor_journal_record_placement(8, PckA_EditorPlaceDoor, /*model*/1, 0, 0, 0, 100, 200);
        REQUIRE(editor_journal_undo_count() == 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Door:"));
    }
}

TEST_CASE_METHOD(JournalTestActive, "record_rect_terrain describes each rect-op pcktype", "[kfx_editor][journal]")
{
    struct EditorRectSlabSnapshot before[1] = {{0, 0}};

    SECTION("place terrain rect") {
        editor_journal_record_rect_terrain(PckA_EditorPlaceTerrainRect, 1, 1, 1, 1, 11, 0, before, 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Terrain Rect"));
    }
    SECTION("clear earth") {
        editor_journal_record_rect_terrain(PckA_EditorRectClearEarth, 1, 1, 1, 1, 2, 0, before, 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Clear Earth"));
    }
    SECTION("set owner") {
        editor_journal_record_rect_terrain(PckA_EditorRectSetOwner, 1, 1, 1, 1, 0, 1, before, 1);
        const char *label = editor_journal_describe_undo(0);
        REQUIRE(label != nullptr);
        CHECK(contains(label, "Set Owner"));
    }
}

TEST_CASE_METHOD(JournalTestActive, "describe_undo/describe_redo return NULL out of range", "[kfx_editor][journal]")
{
    CHECK(editor_journal_describe_undo(0) == nullptr);
    CHECK(editor_journal_describe_redo(0) == nullptr);

    editor_journal_record_placement(1, PckA_EditorPlaceObject, 1, 2, 3, 0, 0, 0);
    CHECK(editor_journal_describe_undo(0) != nullptr);
    CHECK(editor_journal_describe_undo(1) == nullptr);
}

TEST_CASE_METHOD(JournalTestActive, "do_undo sends PckA_EditorUndo with the placed thing's index, and moves the entry to the redo stack", "[kfx_editor][journal]")
{
    editor_journal_record_placement(42, PckA_EditorPlaceObject, 1, 2, 3, 0, 0, 0);
    REQUIRE(editor_journal_undo_count() == 1);

    editor_journal_do_undo();

    CHECK(editor_journal_undo_count() == 0);
    CHECK(editor_journal_redo_count() == 1);

    struct Packet *pckt = get_local_packet();
    CHECK(pckt->action == PckA_EditorUndo);
    CHECK(pckt->actn_par1 == 42);
}

TEST_CASE_METHOD(JournalTestActive, "do_redo resends the right verb for each ambient-position placement pcktype", "[kfx_editor][journal]")
{
    SECTION("creature -> RedoCreature, position/model/owner remapped into par1-4") {
        editor_journal_record_placement(1, PckA_CheatMakeCreature, /*model*/7, /*owner*/2, 0, 0, /*pos_x*/300, /*pos_y*/400);
        editor_journal_do_undo();
        editor_journal_do_redo();
        struct Packet *pckt = get_local_packet();
        CHECK(pckt->action == PckA_EditorRedoCreature);
        CHECK(pckt->actn_par1 == 300);
        CHECK(pckt->actn_par2 == 400);
        CHECK(pckt->actn_par3 == 7);
        CHECK(pckt->actn_par4 == 2);
    }
    SECTION("digger -> RedoDigger") {
        editor_journal_record_placement(1, PckA_CheatMakeDigger, 0, 1, 0, 0, 300, 400);
        editor_journal_do_undo();
        editor_journal_do_redo();
        CHECK(get_local_packet()->action == PckA_EditorRedoDigger);
    }
    SECTION("trap -> RedoTrap") {
        editor_journal_record_placement(1, PckA_EditorPlaceTrap, 1, 0, 0, 0, 300, 400);
        editor_journal_do_undo();
        editor_journal_do_redo();
        CHECK(get_local_packet()->action == PckA_EditorRedoTrap);
    }
    SECTION("door -> RedoDoor") {
        editor_journal_record_placement(1, PckA_EditorPlaceDoor, 1, 0, 0, 0, 300, 400);
        editor_journal_do_undo();
        editor_journal_do_redo();
        CHECK(get_local_packet()->action == PckA_EditorRedoDoor);
    }
    SECTION("object resends verbatim -- already fully explicit, no ambient position to remap") {
        editor_journal_record_placement(1, PckA_EditorPlaceObject, 111, 222, 5, 3, 0, 0);
        editor_journal_do_undo();
        editor_journal_do_redo();
        struct Packet *pckt = get_local_packet();
        CHECK(pckt->action == PckA_EditorPlaceObject);
        CHECK(pckt->actn_par1 == 111);
        CHECK(pckt->actn_par2 == 222);
        CHECK(pckt->actn_par3 == 5);
        CHECK(pckt->actn_par4 == 3);
    }
}

TEST_CASE_METHOD(JournalTestActive, "stack_push drops the oldest entry once the journal exceeds its capacity, without corrupting survivors", "[kfx_editor][journal]")
{
    // Regression test for the memmove-on-a-vector-owning-struct bug fixed
    // earlier in this same effort: EditorJournalEntry owns a std::vector
    // (rect_before), so overflow eviction must move-construct each
    // surviving slot, not raw-copy the struct's bytes between them -- a
    // memmove would corrupt every survivor's vector internals.
    const int64_t kJournalCapacity = 200; // matches editor_journal.cpp's own private constant
    for (int64_t i = 1; i <= kJournalCapacity + 1; i++) {
        editor_journal_record_placement(i, PckA_EditorPlaceObject, i, 0, 0, 0, 0, 0);
    }
    REQUIRE(editor_journal_undo_count() == kJournalCapacity);

    // thing_idx=1 was the oldest and should have been evicted; thing_idx=2
    // is now the oldest survivor. do_undo() pops LIFO and stamps
    // actn_par1 with the popped entry's thing_idx, so popping every
    // survivor but the last and checking that final pop confirms both the
    // eviction and that every surviving entry's data (and vector state)
    // rode out the shift intact.
    for (int64_t i = 0; i < kJournalCapacity - 1; i++) {
        editor_journal_do_undo();
    }
    REQUIRE(editor_journal_undo_count() == 1);

    editor_journal_do_undo();
    CHECK(get_local_packet()->actn_par1 == 2);
    CHECK(editor_journal_undo_count() == 0);
}
