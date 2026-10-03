// Refactor pass 5, S03: golden hashes of what the Lua API does (S02's coverage: lua_api.c 5%, lua_api_things.c
// 3%; three bugs, P5-F5..F7, were in Lua code no test reached).
//
// keeporig level 11 is saved once; each case loads that save, runs a Lua chunk in the level's Lua state, and
// hashes what changed in the game state (ftest_golden.h) together with the chunk's result: what it returned, or
// the error it raised (pcall). The chunks call every global function the API registers (lua_api.c's
// global_methods, the sound and lens functions), with typical arguments and a wrong one; read every field of a
// creature (thing_get_field) and set every settable one and read it back (thing_set_field); call the thing
// methods; and read and write the player, slab, room, camera and map objects' fields.
//
// Helpers every chunk can use (defined before it runs): C0() is player 0's creature with the lowest thing index,
// HERO() the heroes' (nil when there is none); __golden.ser() turns results into text (Things as Thing#index,
// Players as Player#id, tables one level deep with sorted keys).
//
// To print the hashes (only on a commit meant to change what a Lua call does): KFX_FTEST_GOLDEN_PRINT=1 in the
// tree StageFtestData.cmake staged (ftest_golden.h); the lines are logged with the prefix "GOLDEN:", each with the
// start of its result.
#include "ftest_lua_api_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdio.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "config_keeperfx.h"
#include "lua_base.h"
#include <lauxlib.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 5

static const char golden_lua_helpers[] =
    "__golden = {}\n"
    "function __golden.ser(v, d)\n"
    "  local t = type(v)\n"
    "  if t == 'table' then\n"
    "    local ti = rawget(v, 'ThingIndex'); if ti then return 'Thing#' .. tostring(ti) end\n"
    "    local pi = rawget(v, 'playerId'); if pi then return 'Player#' .. tostring(pi) end\n"
    "    if d >= 2 then return '{...}' end\n"
    "    local keys = {}\n"
    "    for k in pairs(v) do keys[#keys + 1] = k end\n"
    "    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)\n"
    "    local out = {}\n"
    "    for i, k in ipairs(keys) do\n"
    "      if i > 12 then out[#out + 1] = '...(' .. #keys .. ')'; break end\n"
    "      out[#out + 1] = tostring(k) .. '=' .. __golden.ser(rawget(v, k), d + 1)\n"
    "    end\n"
    "    return '{' .. table.concat(out, ',') .. '}'\n"
    "  elseif t == 'string' then return string.format('%q', v)\n"
    "  elseif t == 'number' or t == 'boolean' or t == 'nil' then return tostring(v)\n"
    "  else return t end\n"
    "end\n"
    "function __golden.run(f)\n"
    "  local r = {pcall(f)}\n"
    "  if not r[1] then return 'error: ' .. tostring(r[2]) end\n"
    "  local out = {}\n"
    "  for i = 2, table.maxn(r) do out[#out + 1] = __golden.ser(r[i], 0) end\n"
    "  return 'ok: ' .. table.concat(out, ', ')\n"
    "end\n"
    "function __golden.first(plyr)\n"
    "  local best\n"
    "  for _, c in ipairs(GetThingsOfClass('Creature')) do\n"
    "    if c.owner == plyr and (best == nil or c.ThingIndex < best.ThingIndex) then best = c end\n"
    "  end\n"
    "  return best\n"
    "end\n"
    "function C0() return __golden.first(PLAYER0) end\n"
    "function HERO() return __golden.first(PLAYER_GOOD) end\n";

/** Global functions (lua_api.c's global_methods, lua_api_sound.c, lua_api_lens.c) and the objects' methods. */
static const char *const golden_chunks[] = {
    // players and the level's setup
    "SetGenerateSpeed(200)", "SetGenerateSpeed(200, PLAYER0)",
    "ComputerPlayer(PLAYER1, 0)", "ComputerPlayer(PLAYER1, 'ROAMING')",
    "AllyPlayers(PLAYER0, PLAYER_GOOD, 1)", "AllyPlayers(PLAYER0, PLAYER_GOOD)",
    "StartMoney(PLAYER0, 5000)", "StartMoney(ALL_PLAYERS, 0)",
    "MaxCreatures(PLAYER0, 3)",
    "AddCreatureToPool('TROLL', 4)", "AddCreatureToPool('NOT_A_CREATURE', 4)",
    "CreatureAvailable(PLAYER0, 'TROLL', true, 2)",
    "DeadCreaturesReturnToPool(false)",
    "RoomAvailable(PLAYER0, 'TEMPLE', 1, true)", "RoomAvailable(PLAYER0, 'NOT_A_ROOM', 1, true)",
    "MagicAvailable(PLAYER0, 'POWER_LIGHTNING', true, true)",
    "DoorAvailable(PLAYER0, 'BRACED', true, 3)",
    "TrapAvailable(PLAYER0, 'BOULDER', true, 2)",
    "WinGame(PLAYER0)", "WinGame()", "LoseGame(PLAYER0)",
    "return CountCreaturesAtActionPoint(1, PLAYER0, 'ANY_CREATURE')",
    "return CountCreaturesAtActionPoint(1, ALL_PLAYERS, 'IMP')",
    // timers
    "SetTimer(PLAYER0, 'TIMER0')", "AddToTimer(PLAYER0, 'TIMER0', 100)", "DisplayTimer(PLAYER0, 'TIMER0', 1000, true)",
    "HideTimer()", "BonusLevelTime(1000)", "BonusLevelTime(1000, 1)", "BonusLevelTime(1000, 0)", "BonusLevelTime(1000, true)",
    "AddBonusTime(500)",
    // action points, next level
    "ResetActionPoint(1, PLAYER0)", "SetNextLevel(12)", "TriggerActionPoint(1, PLAYER0)",
    "return IsActionpointActivatedByPlayer(PLAYER0, 1)",
    // creatures and parties
    "return AddCreatureToLevel(PLAYER0, 'TROLL', 1, 3, 100)",
    "return AddCreatureToLevel(PLAYER_GOOD, 'KNIGHT', PLAYER0, 5, 0)",
    "return AddCreatureToLevel(PLAYER0, 'IMP', {stl_x = 40, stl_y = 40}, 1, 0)",
    "return AddCreatureToLevel(PLAYER0, 'NOT_A_CREATURE', 1, 1, 0)",
    "return AddTunnellerToLevel(PLAYER_GOOD, 1, 'DUNGEON_HEART', 0, 2, 100)",
    "CreateParty('GOLDEN')",
    "CreateParty('GOLDEN') AddToParty('GOLDEN', 'KNIGHT', 3, 100, 'ATTACK_DUNGEON_HEART', 0)",
    "CreateParty('GOLDEN') AddToParty('GOLDEN', 'KNIGHT', 3, 100, 'ATTACK_DUNGEON_HEART', 0) DeleteFromParty('GOLDEN', 'KNIGHT', 3)",
    "CreateParty('GOLDEN') AddToParty('GOLDEN', 'ARCHER', 2, 0, 'ATTACK_DUNGEON_HEART', 0) return AddPartyToLevel(PLAYER_GOOD, 'GOLDEN', 1)",
    "CreateParty('GOLDEN') AddToParty('GOLDEN', 'ARCHER', 2, 0, 'ATTACK_DUNGEON_HEART', 0) AddTunnellerPartyToLevel(PLAYER_GOOD, 'GOLDEN', 1, 'DUNGEON_HEART', 0, 1, 0)",
    "return AddPartyToLevel(PLAYER_GOOD, 'NO_SUCH_PARTY', 1)",
    // messages and readouts
    "DisplayObjective(1)", "DisplayObjective(1, 1)", "DisplayObjectiveWithPos(1, 40, 40)",
    "DisplayInformation(2)", "DisplayInformationWithPos(2, 40, 40)",
    "QuickObjective('golden objective')", "QuickObjectiveWithPos('golden', 40, 40)",
    "QuickInformation(1, 'golden info')", "QuickInformationWithPos(1, 'golden', 40, 40)",
    "DisplayPlayerObjective(1, PLAYER0)", "DisplayPlayerObjectiveWithPos(1, PLAYER0, 40, 40)",
    "DisplayPlayerInformation(2, PLAYER0)", "DisplayPlayerInformationWithPos(2, PLAYER0, 40, 40)",
    "QuickPlayerObjective('golden', PLAYER0)", "QuickPlayerObjectiveWithPos('golden', PLAYER0, 40, 40)",
    "QuickPlayerInformation(1, PLAYER0, 'golden')", "QuickPlayerInformationWithPos(1, PLAYER0, 'golden', 40, 40)",
    "DisplayMessage(1)", "DisplayMessage(1, PLAYER0)", "DisplayMessage(1, 'IMP')",
    "QuickMessage('golden')", "QuickMessage('golden', PLAYER0)", "QuickMessage('golden') ClearMessage()", "ClearMessage(1)",
    "HeartLostObjective(1, PLAYER0)", "HeartLostQuickObjective('golden', PLAYER0)", // the binding documents (msg, location)
    "HeartLostQuickObjective(1, 'golden', PLAYER0)",
    "PlayMessage(PLAYER0, 'SOUND', 1)", "PlayMessage(PLAYER0, 'SPEECH', 1)",
    "TutorialFlashButton('TREASURE', 100)", "TutorialFlashButton(5, 100)",
    "DisplayCountdown(PLAYER0, 'TIMER0', 500, false)",
    "DisplayVariable(PLAYER0, 'MONEY')", "DisplayVariable(PLAYER0, 'MONEY', 10000, 0)",
    "DisplayVariableWithLabel(PLAYER0, 'TOTAL_CREATURES', 'IMP')",
    "DisplayVariable(PLAYER0, 'MONEY') HideVariable(PLAYER0, 'MONEY')",
    "DisplayVariable(PLAYER0, 'NOT_A_VARIABLE')",
    "SetBoxTooltip(1, 'golden')", "SetBoxTooltipId(1, 5)",
    // the map
    "RevealMapLocation(PLAYER0, 1, 5)", "RevealMapRect(PLAYER0, 40, 40, 10, 10)",
    "ConcealMapRect(PLAYER0, 40, 40, 10, 10)", "ConcealMapRect(PLAYER0, 40, 40, 10, 10, true)",
    "PlaceDoor(PLAYER0, 'WOOD', 33, 37, true, true)",
    "PlaceDoor(PLAYER0, 'WOOD', 33, 37, true, true) SetDoor('UNLOCKED', 33, 37)",
    "SetDoor('LOCKED', 33, 37)",
    "PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true)",
    "ChangeSlabOwner(33, 37, PLAYER_GOOD)", "ChangeSlabOwner(33, 37, PLAYER_GOOD, 'MATCH')",
    "ChangeSlabType(33, 37, 'LAVA')", "ChangeSlabType(33, 37, 'GOLD', 'FLOOR')",
    "ChangeSlabTexture(33, 37, 'ANCIENT')",
    "HideHeroGate(1, true)",
    "AddHeartHealth(PLAYER0, -100, true)", "AddHeartHealth(PLAYER0, 100, false)",
    "MakeSafe(PLAYER0)", "MakeUnsafe(PLAYER0)", "LocateHiddenWorld()",
    // things on the map
    "return AddObjectToLevel('GOLD_CHEST', 1, 500)", "return AddObjectToLevel('GOLD_CHEST', 1, 500, PLAYER0, 0)",
    "return AddObjectToLevelAtPos('BARREL', 100, 112, 0)", "return AddObjectToLevel('NOT_AN_OBJECT', 1, 0)",
    "return AddShotToLevel('SHOT_FIREBALL', 1, PLAYER0, 'CreaturesOnly')",
    "AddEffectGeneratorToLevel('EFFECTGENERATOR_LAVA', 1, 5)",
    "return AddCorpseToLevel('TROLL', 1, 2)", "return AddCorpseToLevel('TROLL', 1, 2, true, PLAYER0)",
    "return CreateEffect('EFFECT_EXPLOSION_1', 1, 0)", "return CreateEffectAtPos('EFFECT_EXPLOSION_1', 40, 40, 0)",
    "CreateEffectsLine(1, 2, 0, 1, 1, 'EFFECT_EXPLOSION_1')",
    // configuration
    "NewCreatureType('GOLDEN')", "CopyCreatureType('TROLL', 'GOLDEN')",
    "SetDoorConfiguration('WOOD', 'Health', 999)", "SetObjectConfiguration('BARREL', 'Health', 999)",
    "SetTrapConfiguration('BOULDER', 'ManufactureLevel', 3)",
    "SetEffectGeneratorConfiguration('EFFECTGENERATOR_LAVA', 'GenerationDelayMin', 5)",
    "SetRoomConfiguration('TREASURE', 'Cost', 100)",
    // documented in the bindings but not registered (P5-F8)
    "Set_creature_configuration('IMP', 'Health', 999)", "SetCreatureProperty('IMP', 'FLYING', true)",
    "ResearchOrder(PLAYER0, 'ROOM', 'TEMPLE', 100)", "ChangeCreatureOwner(C0(), PLAYER_GOOD)",
    "SetGameRule('GoldPerGoldBlock', 2000)", "SetGameRule('NotARule', 1)",
    "SetHandRule(PLAYER0, 'IMP', 0, 'DENY', 'ALWAYS', 0)",
    "SwapCreature('TROLL', 'DRAGON')",
    "SetSacrificeRecipe('MKCREATURE', 'TROLL', 'FLY', 'BUG')", "RemoveSacrificeRecipe('FLY', 'BUG')",
    "SetCreatureInstance('IMP', 1, 'FIREBALL', 1)", "SetCreatureMaxLevel(PLAYER0, 'IMP', 5)",
    "SetCreatureTendencies(PLAYER0, 'IMPRISON', true)", // the binding documents a boolean
    "SetCreatureTendencies(PLAYER0, 'IMPRISON', 1)", "SetCreatureTendencies(PLAYER0, 'FLEE', 0)",
    "CreatureEntranceLevel(PLAYER0, 3)", "ChangeCreaturesAnnoyance(PLAYER0, 'IMP', 'SET', 100)",
    "Research(PLAYER0, 'MAGIC', 'POWER_LIGHTNING', 100)", "Research(PLAYER0, 'ROOM', 'TEMPLE', 100)",
    "Research_order(PLAYER0, 'ROOM', 'TEMPLE', 100)",
    "SetDigger(PLAYER0, 'TROLL')", "SetTexture(PLAYER0, 'ANCIENT')", "SetHandGraphic(PLAYER0, 'Default')",
    // the computer player
    "ComputerDigToLocation(PLAYER1, 1, 2)",
    "SetComputerProcess(PLAYER1, 'BUILD ALL ROOM 3x3', 0, 1, 2, 3, 4)",
    "SetComputerChecks(PLAYER1, 'CHECK MONEY', 100, 1, 2, 3, 4)",
    "SetComputerGlobals(PLAYER1, 1, 2, 3, 4, 5, 6, 7)",
    "SetComputerEvent(PLAYER1, 'EVENT DUNGEON BREACH', 100, 1, 2, 3, 0)",
    // specials and powers
    "UseSpecialIncreaseLevel(PLAYER0)", "UseSpecialIncreaseLevel(PLAYER0, -1)",
    "UseSpecialMultiplyCreatures(PLAYER0, 1)", "UseSpecialTransferCreature(PLAYER0)",
    "UsePower(PLAYER0, 'POWER_OBEY', true)",
    "UsePowerAtLocation(PLAYER0, 1, 'POWER_LIGHTNING', 1, true)",
    "UsePowerAtPos(PLAYER0, 40, 40, 'POWER_CAVE_IN', 1, true)",
    "UsePowerOnCreature(C0(), 'POWER_SPEED', 1, PLAYER0, true)",
    "UseSpellOnCreature(C0(), 'SPELL_ARMOUR', 1)",
    "return PayForPower(PLAYER0, 'POWER_LIGHTNING', 1, false)",
    // the view, music, possession
    "SetMusic(2)", "ZoomToLocation(PLAYER0, 1)", "LockPossession(PLAYER0, true)",
    // running script lines, printing
    "print('golden')", "RunDKScriptCommand('SET_FLAG(PLAYER0,FLAG0,7)')", "RunDKScriptCommand('NOT_A_COMMAND(1)')",
    // queries
    "return GetCreatureNear(40, 40)", "return GetCreatureByCriterion(PLAYER0, 'IMP', 'MOST_EXPERIENCED')",
    "return GetThingByIdx(1)", "return #GetThingsOfClass('Creature')", "return GetThingsOnSubtile(40, 40)",
    "return GetThingsOnSlab(13, 13)", "return GetString(1)", "return GetFloorHeight(40, 40)",
    "return GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')",
    // sound
    "return LoadCustomSound('golden', 'no_such_file.wav')", "return GetCustomSoundId('golden')",
    "return IsCustomSoundLoaded('golden')", "SetCreatureSound('IMP', 'HURT', 'golden')",
    "return PlaySound(1)", "StopSound(1)", "PlayMusic(1)", "StopMusic()",
    // lenses
    "return CreateLens('golden')", "return GetActiveLens()", "return SetActiveLens(0)", "return IsLensEnabled('MIST')",
    "return BuildDarkeningLUT(0.5)",
    // the player object
    "return PLAYER0.MONEY", "PLAYER0.MONEY = 1234 return PLAYER0.MONEY", "PLAYER0.MONEY = 25000 return PLAYER0.MONEY",
    "PLAYER0.HEART_HEALTH = 1000 return PLAYER0.HEART_HEALTH, PLAYER0.heart.health",
    "PLAYER0.max_creatures = 5 return PLAYER0.max_creatures", "PLAYER0.GAME_TURN = 5 return PLAYER0.GAME_TURN",
    "PLAYER0.FLAG3 = 9 return PLAYER0.FLAG3", "PLAYER0.NOT_A_FIELD = 1", "return PLAYER0.type", "return PLAYER1.type",
    "return PLAYER_GOOD.type", "return PLAYER0.max_creatures", "return PLAYER0.player_name",
    "PLAYER0.player_name = 'golden' return PLAYER0.player_name", "return PLAYER0.colour",
    "PLAYER0.colour = 'BLUE' return PLAYER0.colour", "return PLAYER0.heart",
    "return PLAYER0.available('IMP')", "return PLAYER0:available('IMP')", "return PLAYER0:available('TOTAL_CREATURES')",
    "return PLAYER0:controls('IMP')", "return PLAYER0.camera",
    "PLAYER0:add_gold(500) return PLAYER0.MONEY", "PLAYER0:set_texture('ANCIENT')",
    "return tostring(PLAYER0)", "return PLAYER0 == PLAYER0", "return PLAYER0 == PLAYER1", "return PLAYER0.NOT_A_FIELD",
    // the map, slab, room and camera objects
    "return Map.width, Map.height, Map.map_number, Map.map_name, Map.map_type, Map.campaign, Map.default_texture",
    "return Map.creature_pool", "Map.width = 10 return Map.width",
    "local s = GetSlab(13, 13) return s.kind, s.owner, s.revealed, s.style, s.room, s.centerpos",
    "local s = GetSlab(13, 13) s.kind = 'LAVA' return s.kind", "local s = GetSlab(13, 13) s.owner = PLAYER_GOOD return s.owner",
    "return GetSlab(13, 13):GET_CREATURES()", "return GetSlab(500, 500)",
    "local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] return r.type, r.owner, r.slabs_count, r.health, r.max_health, r.efficiency",
    "local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] return r.used_capacity, r.max_capacity, r.capacity_used_for_storage, r.centerpos, r.workers, r.slabs",
    "local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] r.health = 10 return r.health",
    "local c = PLAYER0.camera return c.pos, c.yaw, c.pitch, c.roll, c.zoom, c.horizontal_fov, c.view_mode",
    "PLAYER0.camera.zoom = 2000 return PLAYER0.camera.zoom",
    // the thing methods
    "return C0():isValid()", "return tostring(C0())", "return C0() == C0()", "return C0().NOT_A_FIELD",
    "C0():walk_to(40, 40)", "C0():kill()", "C0():stun(100)", "C0():remove_from_play(100)", "C0():destroy()",
    "C0():delete()", "C0():transform('TROLL', 2)", "C0():transfer(PLAYER_GOOD)", "C0():level_up(2)",
    "C0():teleport(1, 'EFFECT_EXPLOSION_1')", "C0():change_owner(PLAYER_GOOD)", "C0():set_velocity(10, 0, 0)",
    "return C0():get_annoyance('NOT_PAID')", "C0():set_annoyance('HUNGRY', 100) return C0():get_annoyance('HUNGRY')",
    "return C0():in_enemy_custody()",
    "C0():make_thing_zombie()", "C0():set_start_state('CREATURE_STATE_IDLE')",
    "return HERO()", "return PLAYER0.heart.health, PLAYER0.heart.max_health, PLAYER0.heart.thing_class, PLAYER0.heart.model",
    "PLAYER0.heart.health = 100 return PLAYER0.heart.health",
    // a trap's fields (a creature has none of these)
    "PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true) local t = GetThingsOnSubtile(100, 112, 'Trap')[1] return t, t.shots, t.revealed, t.rearm_turn, t.shooting_finished_turn",
    "PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true) local t = GetThingsOnSubtile(100, 112, 'Trap')[1] t.shots = 5 t.revealed = 1 t.rearm_turn = 10 return t.shots, t.revealed, t.rearm_turn",
};

/** The creature fields thing_get_field() reads; each read from C0(). */
static const char *const golden_get_fields[] = {
    "ThingIndex", "creation_turn", "model", "owner", "pos", "orientation", "pitch", "health", "anim_sprite",
    "anim_speed", "sprite_size", "sprite_size_min", "sprite_size_max", "transformation_speed", "clipbox_size_xy",
    "clipbox_size_z", "solid_size_xy", "solid_size_z", "max_health", "picked_up", "thing_class", "parent", "name",
    "gold_held", "party", "level", "max_speed", "exp_points", "creature_kills", "creature_kills_enemies",
    "creature_kills_allies", "hunger_amount", "hunger_level", "hunger_loss", "lair", "opponents_melee_count",
    "opponents_ranged_count", "opponents_count", "battle_enemy", "combat_type", "battle_id",
    "force_health_flower_displayed", "force_health_flower_hidden", "hand_blocked_turns", "countdown", "state",
    "state_besides_interruptions", "continue_state", "instance", "workroom", "moveto_pos", "flee_pos", "patrol_pos",
    "patrol_countdown", "party_objective", "party_original_objective", "party_target_player", "conscious_back_turns",
    "unsummon_duration", "familiars", "shots", "revealed", "rearm_turn", "shooting_finished_turn", "box_kind",
    "target", "damage", "originpos",
};

/** The fields thing_set_field() writes, with a value; each set on C0() and read back. */
static const char *const golden_set_fields[][2] = {
    {"orientation", "1024"}, {"pitch", "100"}, {"owner", "PLAYER_GOOD"}, {"health", "50"}, {"health", "5000"},
    {"health", "-1"},
    {"pos", "{val_x = c.pos.val_x + 256, val_y = c.pos.val_y, val_z = c.pos.val_z}"},
    {"anim_sprite", "1"}, {"anim_speed", "64"}, {"sprite_size", "200"}, {"sprite_size_min", "100"},
    {"sprite_size_max", "300"}, {"transformation_speed", "10"}, {"clipbox_size_xy", "100"}, {"clipbox_size_z", "100"},
    {"solid_size_xy", "100"}, {"solid_size_z", "100"}, {"name", "'Golden'"}, {"gold_held", "500"},
    {"exp_points", "1000"}, {"moveto_pos", "c.pos"}, {"flee_pos", "c.pos"}, {"max_speed", "100"},
    {"patrol_pos", "c.pos"}, {"patrol_countdown", "10"}, {"party_objective", "'ATTACK_DUNGEON_HEART'"},
    {"party_original_objective", "'ATTACK_DUNGEON_HEART'"}, {"party_target_player", "PLAYER0"}, {"countdown", "100"},
    {"state", "1"}, {"continue_state", "1"}, {"instance", "1"}, {"hunger_amount", "5"}, {"hunger_level", "5"},
    {"creature_kills", "3"}, {"creature_kills_allies", "1"}, {"creature_kills_enemies", "2"}, {"hunger_loss", "1"},
    {"hand_blocked_turns", "10"}, {"force_health_flower_displayed", "true"}, {"force_health_flower_hidden", "true"},
    {"conscious_back_turns", "10"}, {"unsummon_duration", "100"}, {"shots", "3"}, {"revealed", "true"},
    {"rearm_turn", "10"}, {"shooting_finished_turn", "10"}, {"box_kind", "1"}, {"target", "c"}, {"damage", "5"},
};

#define GOLDEN_COUNT(a) ((int64_t)(sizeof(a) / sizeof((a)[0])))
#define GOLDEN_CASES (GOLDEN_COUNT(golden_chunks) + GOLDEN_COUNT(golden_get_fields) + GOLDEN_COUNT(golden_set_fields))

static struct FTestGolden s_golden;
static int64_t s_next;

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_lua_api_golden.inc"
    {NULL, 0}
};

/** The i-th case's chunk. */
static void golden_case_chunk(int64_t i, char *chunk, size_t n)
{
    if (i < GOLDEN_COUNT(golden_chunks))
    {
        snprintf(chunk, n, "%s", golden_chunks[i]);
        return;
    }
    i -= GOLDEN_COUNT(golden_chunks);
    if (i < GOLDEN_COUNT(golden_get_fields))
    {
        snprintf(chunk, n, "return C0().%s", golden_get_fields[i]);
        return;
    }
    i -= GOLDEN_COUNT(golden_get_fields);
    snprintf(chunk, n, "local c = C0() c.%s = %s return c.%s", golden_set_fields[i][0], golden_set_fields[i][1],
        golden_set_fields[i][0]);
}

/** Runs the chunk in the level's Lua state, protected; result: "ok: <values>", "error: <message>" or why it
 *  couldn't run. */
static void golden_run_chunk(const char *chunk, char *result, size_t n)
{
    lua_State *L = Lvl_script;
    if (L == NULL)
    {
        snprintf(result, n, "no Lua state");
        return;
    }
    const int top = lua_gettop(L);
    if (luaL_dostring(L, golden_lua_helpers) != 0)
    {
        snprintf(result, n, "helpers: %s", lua_tostring(L, -1));
        lua_settop(L, top);
        return;
    }
    lua_getglobal(L, "__golden");
    lua_getfield(L, -1, "run");
    if (luaL_loadstring(L, chunk) != 0)
    {
        snprintf(result, n, "syntax: %s", lua_tostring(L, -1));
        lua_settop(L, top);
        return;
    }
    if (lua_pcall(L, 1, 1, 0) != 0)
        snprintf(result, n, "failed: %s", lua_tostring(L, -1));
    else
        snprintf(result, n, "%s", lua_tostring(L, -1));
    lua_settop(L, top);
}

FTestActionResult ftest_lua_api_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        FTESTLOG("%" PRId64 " Lua cases", (int64_t)GOLDEN_CASES);
    }
    // a batch of cases per turn (each loads the save, so the turn's own processing doesn't matter)
    for (int64_t batch = 0; (batch < 8) && (s_next < GOLDEN_CASES); batch++, s_next++)
    {
        char chunk[512];
        golden_case_chunk(s_next, chunk, sizeof(chunk));
        if (!ftest_golden_selected(chunk))
            continue;
        if (!ftest_golden_load(&s_golden))
        {
            FTEST_FAIL_TEST("load_game failed");
            return FTRs_Go_To_Next_Action;
        }
        char result[2048];
        golden_run_chunk(chunk, result, sizeof(result));
        char name[600];
        snprintf(name, sizeof(name), "lua:%s", chunk);
        ftest_golden_check_result(&s_golden, name, result);
    }
    if (s_next < GOLDEN_CASES)
        return FTRs_Repeat_Current_Action;
    ftest_golden_finish(&s_golden, "Lua API");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_lua_api_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_next = 0;
    ftest_append_action(ftest_lua_api_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
