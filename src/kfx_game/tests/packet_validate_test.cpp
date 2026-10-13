// Security pass S05 (docs/security-pass/README.md): what a packet may ask for, judged before the game acts on it
// (packet_validate.h). The limits are given by hand so the cases do not depend on a loaded configuration.
#include <catch2/catch_test_macros.hpp>

#include "packet_validate.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "net_main.h"

#include <cstring>

namespace {

PacketLimits limits()
{
    PacketLimits l{};
    l.things = 12288;
    l.events = 200;
    l.rooms = 511;
    l.battles = 192;
    l.players = 9;
    l.subtiles_x = 170;
    l.subtiles_y = 170;
    l.slab_kinds = 60;
    l.creature_models = 40;
    l.trap_kinds = 12;
    l.door_kinds = 6;
    l.power_kinds = 30;
    l.spell_kinds = 30;
    l.instances = 100;
    l.levels = 10;
    l.roomspace_size = 20;
    l.computer_assist_types = 4;
    return l;
}

Packet make(enum TbPacketAction action, int32_t p1 = 0, int32_t p2 = 0, int16_t p3 = 0, int16_t p4 = 0)
{
    Packet p;
    std::memset(&p, 0, sizeof(p));
    p.action = action;
    p.actn_par1 = p1;
    p.actn_par2 = p2;
    p.actn_par3 = p3;
    p.actn_par4 = p4;
    return p;
}

PacketVerdict check(const Packet &p, bool cheats_refused = false, bool from_host = true, int *bad = nullptr)
{
    const PacketLimits l = limits();
    return packet_check(&l, &p, cheats_refused, from_host, bad);
}

} // namespace

TEST_CASE("an empty action is fine, an unknown one is not", "[kfx_game][packet_validate]") {
    CHECK(check(make(PckA_None, 99999, -5)) == PKT_OK);
    // every number above the last action, and every number that is unused or a network message type
    for (int a = PckA_SetTimestamp + 1; a < 256; a++) {
        INFO(a);
        CHECK(check(make((enum TbPacketAction)a)) == PKT_UNKNOWN_ACTION);
    }
    for (enum TbPacketAction a : {PckA_UnusedSlot003, PckA_UnusedSlot010, PckA_UnusedSlot045, PckA_UnusedSlot121, PckA_Login,
                                   PckA_UserUpdate, PckA_Frame, PckA_Resync})
        CHECK(check(make(a)) == PKT_UNKNOWN_ACTION);
    for (enum TbPacketAction a : {PckA_QuitToMainMenu, PckA_TogglePause, PckA_ToggleLights, PckA_PlyrMsgEnd, PckA_UsePwrObey,
                                   PckA_ToggleComputer, PckA_SetFirstPersonDigMode})
        CHECK(check(make(a)) == PKT_OK);
}

TEST_CASE("every action number is either an action of the game or refused, and the two lists agree with the enum", "[kfx_game][packet_validate]") {
    int known = 0;
    for (int a = 1; a < 256; a++)
        if (check(make((enum TbPacketAction)a)) != PKT_UNKNOWN_ACTION)
            known++;
    // 135 actions as of this table; an action added to the enum has to be added to the table, and this count with it
    CHECK(known == 135);
}

TEST_CASE("a thing, event, room, battle or player index must be one the game has", "[kfx_game][packet_validate]") {
    int bad = 0;
    CHECK(check(make(PckA_EventBoxTurnOff, 199)) == PKT_OK);
    CHECK(check(make(PckA_EventBoxTurnOff, 200), false, true, &bad) == PKT_BAD_PARAMETER);
    CHECK(bad == 1);
    CHECK(check(make(PckA_EventBoxTurnOff, -1)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_EventBoxTurnOff, INT32_MIN)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_ZoomToEvent, 200)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_ZoomToRoom, 510)) == PKT_OK);
    CHECK(check(make(PckA_ZoomToRoom, 511)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_ZoomToBattle, 192)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_UseSpecialBox, 12287)) == PKT_OK);
    CHECK(check(make(PckA_UseSpecialBox, 12288)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_TransferCreatr, 5, 12288), false, true, &bad) == PKT_BAD_PARAMETER);
    CHECK(bad == 2);
    CHECK(check(make(PckA_PlyrToggleAlly, 8)) == PKT_OK);
    CHECK(check(make(PckA_PlyrToggleAlly, 9)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_PlyrToggleAlly, -1)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_UsePwrOnThing, 29, 100)) == PKT_OK);
    CHECK(check(make(PckA_UsePwrOnThing, 30, 100)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CtrlCrtrSetInstnc, 99)) == PKT_OK);
    CHECK(check(make(PckA_CtrlCrtrSetInstnc, 100)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_UsePwrHandDrop, 169, 169)) == PKT_OK);
    CHECK(check(make(PckA_UsePwrHandDrop, 170, 0)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_UsePwrHandDrop, 0, 170)) == PKT_BAD_PARAMETER);
}

TEST_CASE("the size of a room-sized box can't exceed the grid it is drawn on", "[kfx_game][packet_validate]") {
    // the grid is MAX_ROOMSPACE_WIDTH square; a larger width written into it is an out-of-bounds write
    CHECK(check(make(PckA_SetRoomspaceMan, 19)) == PKT_OK);
    CHECK(check(make(PckA_SetRoomspaceMan, 20)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_SetRoomspaceMan, 1000000)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_SetRoomspaceDefault, -3)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_SetRoomspaceHighlight, 2, 19)) == PKT_OK);
    CHECK(check(make(PckA_SetRoomspaceHighlight, 2, 20)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_ApplyRoomspaceDigTag, 7, 0x7fffffff)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_ApplyRoomspaceDigTag, 7, 5)) == PKT_OK);
    // the mode in the first parameter is the handler's to judge
    CHECK(check(make(PckA_SetRoomspaceHighlight, 123456, 3)) == PKT_OK);
}

TEST_CASE("a computer-assist type must be one the game has", "[kfx_game][packet_validate]") {
    // set_autopilot_type() indexes a table with it: 0 and past the end read outside it
    CHECK(check(make(PckA_SetComputerKind, 1)) == PKT_OK);
    CHECK(check(make(PckA_SetComputerKind, 4)) == PKT_OK);
    CHECK(check(make(PckA_SetComputerKind, 0)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_SetComputerKind, 5)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_SetComputerKind, INT32_MIN)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_SetComputerKind, INT32_MAX)) == PKT_BAD_PARAMETER);
}

TEST_CASE("cheat and editor actions are refused where cheats are", "[kfx_game][packet_validate]") {
    for (enum TbPacketAction a : {PckA_CheatEnter, PckA_CheatRevealMap, PckA_CheatMakeDigger, PckA_CheatWinLevel, PckA_EditorUndo,
                                   PckA_EditorRectClearEarth, PckA_AssistCommit, PckA_CheatSwitchPlayer}) {
        INFO(a);
        // parameters that would be fine
        Packet p = make(a, 1, 1, 1, 1);
        CHECK(check(p, /*cheats_refused*/ true) == PKT_CHEAT_REFUSED);
        CHECK(check(p, /*cheats_refused*/ false) == PKT_OK);
    }
    // ordinary actions are not affected
    CHECK(check(make(PckA_TogglePause), true) == PKT_OK);
    CHECK(check(make(PckA_UsePwrHandPick, 5), true) == PKT_OK);
}

TEST_CASE("the host's clock can only come from the host", "[kfx_game][packet_validate]") {
    CHECK(check(make(PckA_SetTimestamp, 1, 2, 3, 4), false, /*from_host*/ true) == PKT_OK);
    CHECK(check(make(PckA_SetTimestamp, 1, 2, 3, 4), false, /*from_host*/ false) == PKT_NOT_ALLOWED_HERE);
}

TEST_CASE("the cheat and editor actions' parameters index nothing outside the tables", "[kfx_game][packet_validate]") {
    // a replay can switch cheats on for itself, so these matter outside multiplayer too
    int bad = 0;
    CHECK(check(make(PckA_CheatKillPlayer, 8)) == PKT_OK);
    CHECK(check(make(PckA_CheatKillPlayer, 9)) == PKT_BAD_PARAMETER);      // keeper_destroyed[par1]++ lives here
    CHECK(check(make(PckA_CheatHeartHealth, 100000)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatHeartHealth, 3, 2000000000)) == PKT_OK);    // the health itself is not an index
    CHECK(check(make(PckA_CheatMakeCreature, 39, 0x0503)) == PKT_OK);      // packed: player 3, experience 5
    CHECK(check(make(PckA_CheatMakeCreature, 40, 3)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatMakeCreature, 5, 0x0509), false, true, &bad) == PKT_BAD_PARAMETER);
    CHECK(bad == 2);
    CHECK(check(make(PckA_CheatMakeDigger, 3, 9)) == PKT_OK);
    CHECK(check(make(PckA_CheatMakeDigger, 3, 10)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatSwitchTerrain, 59)) == PKT_OK);
    CHECK(check(make(PckA_CheatSwitchTerrain, 60)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatSwitchExperience, 10)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatSwitchTrap, 12)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatSwitchDoor, 6)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatApplySpell, 30)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_CheatPlaceTerrain, 3, 9)) == PKT_BAD_PARAMETER);
    // map coordinates are a subtile times 256
    CHECK(check(make(PckA_EditorPlaceObject, 169 * 256 + 255, 0, 5, 3)) == PKT_OK);
    CHECK(check(make(PckA_EditorPlaceObject, 170 * 256, 0, 5, 3)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_EditorPlaceObject, 0, -1, 5, 3)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_EditorPlaceObject, 256, 256, 5, 9), false, true, &bad) == PKT_BAD_PARAMETER);
    CHECK(bad == 4);
    // the third and fourth parameters of an editor action are data (not the camera position that other packets carry)
    CHECK(check(make(PckA_EditorRedoTrap, 512, 512, 11, 3)) == PKT_OK);
    CHECK(check(make(PckA_EditorRedoTrap, 512, 512, 12, 3), false, true, &bad) == PKT_BAD_PARAMETER);
    CHECK(bad == 3);
    CHECK(check(make(PckA_EditorSetThingPosition, 256, 256, 12288)) == PKT_BAD_PARAMETER);
    CHECK(check(make(PckA_EditorPlaceTerrainRect, 10, 10, 5, 2)) == PKT_OK);
    CHECK(check(make(PckA_EditorPlaceTerrainRect, 170, 10, 5, 2)) == PKT_BAD_PARAMETER);
}

TEST_CASE("an action that carries the camera position does not have its third and fourth parameters judged", "[kfx_game][packet_validate]") {
    // UsePwrHandPick (no extra data) shares those bytes with the camera position: any value is the camera's
    CHECK(check(make(PckA_UsePwrHandPick, 5, 0, -32768, 32767)) == PKT_OK);
    CHECK(check(make(PckA_ZoomToRoom, 5, 0, 30000, 30000)) == PKT_OK);
}

TEST_CASE("a rejected packet loses its action and keeps its input", "[kfx_game][packet_validate]") {
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    kfx_sim_state.game_kind = GKind_LocalGame;
    kfx_sim_state.map_subtiles_x = kfx_sim_state.map_subtiles_y = 85;
    Packet p = make(PckA_EventBoxTurnOff, 5000);
    p.control_flags = 0x1234;
    p.pos_x = 777;
    CHECK(packet_validate_for_user(0, &p) == PKT_BAD_PARAMETER);
    CHECK(p.action == PckA_None);
    CHECK(p.control_flags == 0x1234);
    CHECK(p.pos_x == 777);
    Packet fine = make(PckA_EventBoxTurnOff, 5);
    CHECK(packet_validate_for_user(0, &fine) == PKT_OK);
    CHECK(fine.action == PckA_EventBoxTurnOff);
    // a multiplayer game: cheats refused, and only user 0 is the host
    kfx_sim_state.game_kind = GKind_MultiGame;
    Packet cheat = make(PckA_CheatWinLevel);
    CHECK(packet_validate_for_user(1, &cheat) == PKT_CHEAT_REFUSED);
    CHECK(cheat.action == PckA_None);
    Packet clock = make(PckA_SetTimestamp, 1, 2);
    CHECK(packet_validate_for_user(1, &clock) == PKT_NOT_ALLOWED_HERE);
    Packet host_clock = make(PckA_SetTimestamp, 1, 2);
    CHECK(packet_validate_for_user(SERVER_ID, &host_clock) == PKT_OK);
}
