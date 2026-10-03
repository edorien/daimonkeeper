// Refactor pass 5, S03: golden hashes of what the cheats and the editor's packet actions do (S02's coverage:
// game_commands_cheats.c 14%; the packet golden, packet_actions_golden_test.cpp, runs them on a bare level, where
// most of them find nothing to act on).
//
// keeporig level 11 is saved once; each case loads that save and runs its steps as player 0 through
// process_user_packet(), the path a packet takes on every machine, and hashes what changed in the game state
// (ftest_golden.h) together with the replies the steps added to the message list. A step is one of:
//   a:<action> [p1 [p2 [p3 [p4]]]] [@<at>]  a cheat or editor packet action (PckA_<action>), with the cursor at <at>
//   c:<state> [@<at>] [press|release|click] a cheat cursor mode (PSt_<state>): the cursor at <at>, the left button
//                                            pressed, released or both (none: the cursor only hovers)
//   possess                                  player 0 takes control of its creature with the lowest thing index
//   multiplayer                              the game is a multiplayer one (kfx_sim_state.game_kind), which
//                                            refuses cheats (P5-F17)
//   lua:<chunk>                              runs a Lua chunk in the level's Lua state (to define an event handler)
// and a case is "step ; step ; ...". Numbers can be $C0 (player 0's creature with the lowest thing index), $HERO
// (the heroes'), $HEART (player 0's heart), $ROOM (player 0's room with the lowest index), or #<n>: subtile n's
// centre as a map coordinate. <at> is one of those things' position (C0, HERO, HEART, ROOM: the room's centre) or
// a subtile "x,y". The cursor is on C0 unless a step says otherwise.
//
// To print the hashes (only on a commit meant to change what a cheat does): KFX_FTEST_GOLDEN_PRINT=1 in the tree
// StageFtestData.cmake staged (ftest_golden.h); the lines are logged with the prefix "GOLDEN:", each with the
// start of its result.
#include "ftest_cheats_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config_keeperfx.h"
#include "config_players.h"
#include "lua_base.h"
#include <lauxlib.h>
#include "dungeon_data.h"
#include "frontend.h"
#include "game_commands.h"
#include "kfx_sim_state.h"
#include "packet_data.h"
#include "player_data.h"
#include "room_data.h"
#include "thing_creature.h"
#include "thing_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 8

static const char *const golden_cases[] = {
    // the global cheats
    "a:CheatEnter", "a:CheatAllFree", "a:CheatCrtSpells", "a:CheatRevealMap", "a:CheatCrAllSpls", "a:CheatAllMagic",
    "a:CheatAllRooms", "a:CheatAllResrchbl", "a:CheatSwitchTerrain 12", "a:CheatSwitchTerrain 0",
    "a:CheatSwitchPlayer 1", "a:CheatSwitchCreature 5", "a:CheatSwitchHero 3", "a:CheatSwitchExperience 4",
    "a:CheatSwitchTrap 2", "a:CheatSwitchDoor 2", "a:CheatAllDoors", "a:CheatAllTraps", "a:CheatGiveDoorTrap",
    "a:CheatWinLevel", "a:CheatLoseLevel",
    "a:CheatLevelUp", "possess ; a:CheatLevelUp", "a:CheatLevelDown", "possess ; a:CheatLevelDown",
    "a:CheatApplySpell 3", "possess ; a:CheatApplySpell 3", "a:CheatKillCreature", "possess ; a:CheatKillCreature",
    // the dungeon-control cheats
    "a:CheatPlaceTerrain 12 0", "a:CheatPlaceTerrain 11 1 @ROOM", "a:CheatPlaceTerrain 0 5 @40,40",
    "a:CheatMakeCreature 5 0", "a:CheatMakeCreature 5 772", "a:CheatMakeCreature 5 4 @40,40",
    "a:CheatMakeDigger 0 2", "a:CheatMakeDigger 1 0",
    "a:CheatStealSlab 11 1", "a:CheatStealSlab 11 260", "a:CheatStealSlab 4 260", "a:CheatStealSlab 12 0 @40,40",
    "a:CheatStealRoom 1 0 @ROOM", "a:CheatStealRoom 4 1 @ROOM", "a:CheatStealRoom 1 0 @40,40",
    "a:CheatHeartHealth 0 1000", "a:CheatHeartHealth 0 0", "a:CheatHeartHealth 1 500",
    "a:CheatKillPlayer 0", "a:CheatKillPlayer 1",
    "a:CheatConvertCreature 4", "c:ConvertCreatr ; a:CheatConvertCreature 4",
    // the editor's actions
    "a:EditorFloodFill 12 0", "a:EditorFloodFill 11 0 @40,40",
    "a:EditorPlaceObject #120 #128 5 0", "a:EditorPlaceObject #120 #128 3 0", "a:EditorPlaceObject #500 #500 5 0",
    "a:EditorPlaceTrap 1 0", "a:EditorPlaceTrap 2 0 @40,40", "a:EditorPlaceDoor 1 0", "a:EditorPlaceDoor 1 0 @ROOM",
    "a:EditorToggleDoorLock $C0", "a:EditorToggleDoorLock 0",
    "a:EditorUndo $C0", "a:EditorUndo $HEART", "a:EditorUndo 0",
    "a:EditorSetGoldValue $C0 500", "a:EditorSetThingPosition #120 #128 $C0 0",
    "a:EditorPlaceTerrainRect #120 #125 12 0", "a:EditorRectClearEarth #120 #125", "a:EditorRectDeleteThings #120 #125",
    "a:EditorRectSetOwner #120 #125 1", "a:EditorRectSetOwner #120 #125 0 @ROOM",
    "a:EditorRedoCreature #126 #131 5 0", "a:EditorRedoDigger #126 #131 0 1", "a:EditorRedoTrap #126 #131 1 0",
    "a:EditorRedoDoor #126 #131 1 0",
    "a:EditorGoSpectator #126 #131",
    // the cheat cursor modes, hovering and clicking
    "c:MkDigger", "c:MkDigger click", "c:MkDigger click @40,40",
    "c:MkGoodCreatr", "a:CheatSwitchHero 3 ; c:MkGoodCreatr click",
    "c:MkBadCreatr", "a:CheatSwitchCreature 5 ; a:CheatSwitchExperience 3 ; c:MkBadCreatr click",
    "c:MkGoldPot", "c:MkGoldPot click",
    "c:OrderCreatr", "c:OrderCreatr click", "c:OrderCreatr click ; c:OrderCreatr click @ROOM",
    "c:FreeDestroyWalls", "c:FreeDestroyWalls click", "c:FreeDestroyWalls click @40,40",
    "c:FreeTurnChicken click", "c:FreeCastDisease click", "c:FreeCastDisease click @40,40",
    "c:StealRoom @ROOM", "c:StealRoom click @ROOM", "a:CheatSwitchPlayer 1 ; c:StealRoom click @ROOM",
    "c:DestroyRoom click @ROOM", "c:DestroyRoom click",
    "c:KillCreatr", "c:KillCreatr click", "c:KillCreatr click @40,40",
    "c:ConvertCreatr click", "a:CheatSwitchPlayer 4 ; c:ConvertCreatr click",
    "c:StealSlab", "c:StealSlab click", "a:CheatSwitchPlayer 1 ; c:StealSlab click", "c:StealSlab click @40,40",
    "c:LevelCreatureUp click", "c:LevelCreatureDown click", "c:LevelCreatureUp click @40,40",
    "c:KillPlayer @HEART", "c:KillPlayer click @HEART", "c:KillPlayer click",
    "c:HeartHealth @HEART", "c:HeartHealth click @HEART",
    "c:QueryAll", "c:QueryAll click", "c:CreatrInfoAll click",
    "c:MkHappy click", "c:MkAngry click", "c:MkAngry click @40,40",
    "c:PlaceTerrain", "a:CheatSwitchTerrain 12 ; c:PlaceTerrain click", "a:CheatSwitchTerrain 11 ; c:PlaceTerrain click",
    "c:DestroyThing", "c:DestroyThing click", "c:DestroyThing click @HEART", "c:DestroyThing click @40,40",
    "c:EditorPlaceTerrainRect press @120,125 ; c:EditorPlaceTerrainRect release",
    "c:EditorRectClearEarth press @120,125 ; c:EditorRectClearEarth release",
    "c:EditorRectDeleteThings press @120,125 ; c:EditorRectDeleteThings release",
    "c:EditorRectSetOwner press @120,125 ; c:EditorRectSetOwner release",
    "c:EditorFill click", "c:EditorPlaceObject click", "c:EditorEyedropper click", "c:EditorStamp click",
    "c:EditorQuery click", "c:EditorPlaceTrap click", "c:EditorPlaceDoor click", "c:EditorPlaceDoor click @ROOM",
    // a multiplayer game refuses them all: only the game kind changes (P5-F17)
    "multiplayer", "multiplayer ; a:CheatRevealMap", "multiplayer ; a:CheatWinLevel", "multiplayer ; a:CheatAllRooms",
    "multiplayer ; a:CheatMakeCreature 5 0", "multiplayer ; a:CheatPlaceTerrain 12 0", "multiplayer ; a:EditorUndo $C0",
    "multiplayer ; a:EditorRectClearEarth #120 #125", "multiplayer ; possess ; a:CheatKillCreature",
    "multiplayer ; c:KillCreatr click", "multiplayer ; c:DestroyThing click @HEART",
    "multiplayer ; a:SetPlyrState 28 0", "a:SetPlyrState 28 0", "multiplayer ; a:SetPlyrState 2 1",
    // Lua's OnObjectDestroyed is for objects, not a creature the editor or the eraser destroys (P5-F19)
    "lua:function OnObjectDestroyed(t) PLAYER0.FLAG5 = 7 end ; a:EditorUndo $C0",
    "lua:function OnObjectDestroyed(t) PLAYER0.FLAG5 = 7 end ; c:DestroyThing click",
    "lua:function OnObjectDestroyed(t) PLAYER0.FLAG5 = 7 end ; a:EditorRectDeleteThings #120 #125",
    "lua:function OnObjectDestroyed(t) PLAYER0.FLAG5 = 7 end ; a:EditorUndo $HEART",
};

#define GOLDEN_COUNT(a) ((int64_t)(sizeof(a) / sizeof((a)[0])))

struct GoldenName { const char *name; int64_t value; };

#define ACTN(x) {#x, PckA_##x}
static const struct GoldenName golden_actions[] = {
    ACTN(CheatEnter), ACTN(CheatAllFree), ACTN(CheatCrtSpells), ACTN(CheatRevealMap), ACTN(CheatCrAllSpls),
    ACTN(CheatAllMagic), ACTN(CheatAllRooms), ACTN(CheatAllResrchbl), ACTN(CheatSwitchTerrain), ACTN(CheatSwitchPlayer),
    ACTN(CheatSwitchCreature), ACTN(CheatSwitchHero), ACTN(CheatSwitchExperience), ACTN(CheatSwitchTrap),
    ACTN(CheatSwitchDoor), ACTN(CheatAllDoors), ACTN(CheatAllTraps), ACTN(CheatGiveDoorTrap), ACTN(CheatWinLevel),
    ACTN(CheatLoseLevel), ACTN(CheatLevelUp), ACTN(CheatLevelDown), ACTN(CheatApplySpell), ACTN(CheatKillCreature),
    ACTN(CheatPlaceTerrain), ACTN(EditorFloodFill), ACTN(EditorPlaceObject), ACTN(EditorPlaceTrap),
    ACTN(EditorPlaceDoor), ACTN(EditorToggleDoorLock), ACTN(EditorUndo), ACTN(EditorSetGoldValue),
    ACTN(EditorSetThingPosition), ACTN(EditorPlaceTerrainRect), ACTN(EditorRectClearEarth),
    ACTN(EditorRectDeleteThings), ACTN(EditorRectSetOwner), ACTN(CheatMakeCreature), ACTN(CheatMakeDigger),
    ACTN(EditorRedoCreature), ACTN(EditorRedoDigger), ACTN(EditorRedoTrap), ACTN(EditorRedoDoor),
    ACTN(CheatStealSlab), ACTN(CheatStealRoom), ACTN(CheatHeartHealth), ACTN(CheatKillPlayer),
    ACTN(CheatConvertCreature), ACTN(EditorGoSpectator), ACTN(SetPlyrState),
};
#undef ACTN

#define PST(x) {#x, PSt_##x}
static const struct GoldenName golden_states[] = {
    PST(MkDigger), PST(MkGoodCreatr), PST(MkGoldPot), PST(OrderCreatr), PST(MkBadCreatr), PST(FreeDestroyWalls),
    PST(FreeTurnChicken), PST(FreeCastDisease), PST(StealRoom), PST(DestroyRoom), PST(KillCreatr), PST(ConvertCreatr),
    PST(StealSlab), PST(LevelCreatureUp), PST(LevelCreatureDown), PST(KillPlayer), PST(HeartHealth), PST(QueryAll),
    PST(CreatrInfoAll), PST(MkHappy), PST(MkAngry), PST(PlaceTerrain), PST(EditorPlaceTerrainRect),
    PST(EditorRectClearEarth), PST(EditorRectDeleteThings), PST(EditorRectSetOwner), PST(EditorFill),
    PST(EditorPlaceObject), PST(EditorEyedropper), PST(EditorStamp), PST(EditorQuery), PST(EditorPlaceTrap),
    PST(EditorPlaceDoor), PST(DestroyThing),
};
#undef PST

static struct FTestGolden s_golden;
static int64_t s_next;

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_cheats_golden.inc"
    {NULL, 0}
};

static int64_t golden_lookup(const struct GoldenName *names, int64_t count, const char *name)
{
    for (int64_t i = 0; i < count; i++)
    {
        if (strcmp(names[i].name, name) == 0)
            return names[i].value;
    }
    return -1;
}

/** Player plyr's creature with the lowest thing index, or 0. */
static ThingIndex golden_first_creature(PlayerNumber plyr)
{
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
    {
        struct Thing *thing = thing_get(i);
        if (thing_exists(thing) && (thing->class_id == TCls_Creature) && (thing->owner == plyr))
            return i;
    }
    return 0;
}

static RoomIndex golden_first_room(PlayerNumber plyr)
{
    for (RoomIndex i = 1; i < ROOMS_COUNT; i++)
    {
        struct Room *room = room_get(i);
        if (room_exists(room) && (room->owner == plyr))
            return i;
    }
    return 0;
}

/** A number: $C0, $HERO, $HEART, $ROOM, #<subtile> or a literal. */
static int64_t golden_number(const char *word)
{
    if (strcmp(word, "$C0") == 0)
        return golden_first_creature(PLAYER0);
    if (strcmp(word, "$HERO") == 0)
        return golden_first_creature(PLAYER_GOOD);
    if (strcmp(word, "$HEART") == 0)
        return get_player_soul_container(PLAYER0)->index;
    if (strcmp(word, "$ROOM") == 0)
        return golden_first_room(PLAYER0);
    if (word[0] == '#')
        return subtile_coord_center(atoi(word + 1));
    return atoi(word);
}

/** The subtile of <at>: C0, HERO, HEART, ROOM or "x,y". */
static void golden_at(const char *at, MapSubtlCoord *stl_x, MapSubtlCoord *stl_y)
{
    const struct Thing *thing = NULL;
    if (strcmp(at, "C0") == 0)
        thing = thing_get(golden_first_creature(PLAYER0));
    else if (strcmp(at, "HERO") == 0)
        thing = thing_get(golden_first_creature(PLAYER_GOOD));
    else if (strcmp(at, "HEART") == 0)
        thing = get_player_soul_container(PLAYER0);
    else if (strcmp(at, "ROOM") == 0)
    {
        const struct Room *room = room_get(golden_first_room(PLAYER0));
        *stl_x = room->central_stl_x;
        *stl_y = room->central_stl_y;
        return;
    }
    if (thing != NULL)
    {
        *stl_x = thing->mappos.x.stl.num;
        *stl_y = thing->mappos.y.stl.num;
        return;
    }
    *stl_x = atoi(at);
    const char *comma = strchr(at, ',');
    *stl_y = (comma != NULL) ? atoi(comma + 1) : 0;
}

static NetUserId golden_user(void)
{
    NetUserId user = get_player(PLAYER0)->user_id;
    return (user < 0) ? get_local_user() : user;
}

/** Runs one step; appends its replies (or why it couldn't run) to result. */
static void golden_run_step(const char *step, char *result, size_t n)
{
    struct GuiMessage before[GUI_MESSAGES_COUNT];
    memcpy(before, kfx_sim_state.messages, sizeof(before));
    const int64_t count_before = kfx_sim_state.active_messages_count;
    size_t o = strlen(result);
    o += snprintf(result + o, (o < n) ? n - o : 0, "%s", (o > 0) ? " ;" : "");

    if (strncmp(step, "lua:", 4) == 0)
    {
        if ((Lvl_script != NULL) && (luaL_dostring(Lvl_script, step + 4) != 0))
        {
            snprintf(result + o, (o < n) ? n - o : 0, " lua: %s", lua_tostring(Lvl_script, -1));
            lua_pop(Lvl_script, 1);
        }
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", step);
    char *words[8];
    int64_t count = 0;
    for (char *w = strtok(buf, " "); (w != NULL) && (count < 8); w = strtok(NULL, " "))
        words[count++] = w;
    if (count == 0)
        return;
    struct PlayerInfo *player = get_player(PLAYER0);
    if (strcmp(words[0], "multiplayer") == 0)
    {
        kfx_sim_state.game_kind = GKind_MultiGame;
        return;
    }
    if (strcmp(words[0], "possess") == 0)
    {
        TbBool ok = control_creature_as_controller(player, thing_get(golden_first_creature(PLAYER0)));
        snprintf(result + o, (o < n) ? n - o : 0, " possess=%d", ok ? 1 : 0);
        return;
    }
    const NetUserId user = golden_user();
    struct Packet *pckt = get_packet(user);
    MapSubtlCoord stl_x = 0;
    MapSubtlCoord stl_y = 0;
    golden_at("C0", &stl_x, &stl_y);
    int64_t par[4] = {0, 0, 0, 0};
    int64_t npar = 0;
    uint32_t flags = PCtr_MapCoordsValid;
    for (int64_t i = 1; i < count; i++)
    {
        if (words[i][0] == '@')
            golden_at(words[i] + 1, &stl_x, &stl_y);
        else if (strcmp(words[i], "press") == 0)
            flags |= PCtr_LBtnClick | PCtr_LBtnHeld;
        else if (strcmp(words[i], "release") == 0)
            flags |= PCtr_LBtnRelease;
        else if (strcmp(words[i], "click") == 0)
            flags |= PCtr_LBtnClick | PCtr_LBtnRelease;
        else if (npar < 4)
            par[npar++] = golden_number(words[i]);
    }
    memset(pckt, 0, sizeof(*pckt));
    pckt->pos_x = subtile_coord_center(stl_x);
    pckt->pos_y = subtile_coord_center(stl_y);
    if (strncmp(words[0], "a:", 2) == 0)
    {
        int64_t action = golden_lookup(golden_actions, GOLDEN_COUNT(golden_actions), words[0] + 2);
        if (action < 0)
        {
            snprintf(result + o, (o < n) ? n - o : 0, " unknown action %s", words[0] + 2);
            return;
        }
        set_packet_action(pckt, action, par[0], par[1], par[2], par[3]);
        pckt->control_flags = PCtr_MapCoordsValid;
    }
    else if (strncmp(words[0], "c:", 2) == 0)
    {
        int64_t state = golden_lookup(golden_states, GOLDEN_COUNT(golden_states), words[0] + 2);
        if (state < 0)
        {
            snprintf(result + o, (o < n) ? n - o : 0, " unknown state %s", words[0] + 2);
            return;
        }
        player->work_state = state;
        pckt->control_flags = flags;
    }
    else
    {
        snprintf(result + o, (o < n) ? n - o : 0, " unknown step %s", words[0]);
        return;
    }
    process_user_packet(user);
    memset(pckt, 0, sizeof(*pckt));
    // a reply goes in at the front: the new ones are those before the old list's start
    int64_t added = GUI_MESSAGES_COUNT;
    for (int64_t k = 0; k <= GUI_MESSAGES_COUNT; k++)
    {
        TbBool same = (kfx_sim_state.active_messages_count == ((count_before + k < GUI_MESSAGES_COUNT) ?
            count_before + k : GUI_MESSAGES_COUNT));
        for (int64_t i = 0; same && (i + k < GUI_MESSAGES_COUNT); i++)
            same = (memcmp(&kfx_sim_state.messages[i + k], &before[i], sizeof(before[i])) == 0);
        if (same)
        {
            added = k;
            break;
        }
    }
    if (added > kfx_sim_state.active_messages_count)
        added = kfx_sim_state.active_messages_count;
    for (int64_t i = added - 1; i >= 0; i--)
        o += snprintf(result + o, (o < n) ? n - o : 0, " \"%s\"", kfx_sim_state.messages[i].text);
}

/** Runs a case: its steps, separated by " ; ". */
static void golden_run_case(const char *text, char *result, size_t n)
{
    result[0] = '\0';
    char step[256];
    for (;;)
    {
        const char *end = strstr(text, " ; ");
        size_t len = (end != NULL) ? (size_t)(end - text) : strlen(text);
        snprintf(step, sizeof(step), "%.*s", (int)len, text);
        golden_run_step(step, result, n);
        if (end == NULL)
            break;
        text = end + 3;
    }
    // what a load doesn't put back: an event handler a lua: step defined, the cheat menus the cursor modes open
    if (Lvl_script != NULL)
        luaL_dostring(Lvl_script, "OnObjectDestroyed = nil");
    close_main_cheat_menu();
    close_creature_cheat_menu();
    close_instance_cheat_menu();
    close_secondary_cheat_menu();
}

FTestActionResult ftest_cheats_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        FTESTLOG("%" PRId64 " cheat cases", GOLDEN_COUNT(golden_cases));
    }
    // a batch of cases per turn (each loads the save, so the turn's own processing doesn't matter)
    for (int64_t batch = 0; (batch < 8) && (s_next < GOLDEN_COUNT(golden_cases)); batch++, s_next++)
    {
        const char *text = golden_cases[s_next];
        char name[600];
        snprintf(name, sizeof(name), "cheat:%s", text);
        if (!ftest_golden_selected(name))
            continue;
        if (!ftest_golden_load(&s_golden))
        {
            FTEST_FAIL_TEST("load_game failed");
            return FTRs_Go_To_Next_Action;
        }
        char result[2048];
        golden_run_case(text, result, sizeof(result));
        ftest_golden_check_result(&s_golden, name, result);
    }
    if (s_next < GOLDEN_COUNT(golden_cases))
        return FTRs_Repeat_Current_Action;
    ftest_golden_finish(&s_golden, "cheat");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_cheats_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_next = 0;
    ftest_append_action(ftest_cheats_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
