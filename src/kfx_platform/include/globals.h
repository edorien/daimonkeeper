/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file globals.h
 *     KeeperFX global compile config file.
 * @par Purpose:
 *     Header file for global definitions.
 * @par Comment:
 *     Defines basic includes and definitions, used in whole program.
 * @author   Tomasz Lis
 * @date     08 Aug 2008 - 03 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KEEPFX_GLOBALS_H
#define KEEPFX_GLOBALS_H

#include "bflib_basics.h"
#include <stdbool.h> // Introduced in C99. Provides true/false.
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h> // Provides NULL.
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <assert.h>

#if defined(unix) && !defined(GO32)
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#if !defined(stricmp)
#define stricmp strcasecmp
#endif
#if !defined(strnicmp)
#define strnicmp strncasecmp
#endif

#elif defined(MSDOS)
#include <dos.h>
#include <process.h>
#endif

#ifdef _MSC_VER
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp
#endif

#include "version.h"

#ifndef BFDEBUG_LEVEL
#error "BFDEBUG_LEVEL should be defined in version.h"
#define BFDEBUG_LEVEL 0
#endif

#ifdef __cplusplus
#include <algorithm>
using std::min;
using std::max;
using std::clamp;
extern "C" {
#endif

// Basic Definitions

#if defined(unix) && !defined (GO32)
#define SEPARATOR "/"
#else
#define SEPARATOR "\\"
#endif

#ifndef __cplusplus
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef clamp
#define clamp(v,lo,hi) (max(lo, min(v, hi)))
#endif
#endif

// Return values for verification functions
#define VERIF_ERROR   0
#define VERIF_OK      1
#define VERIF_WARN    2

// Return values for all other functions
#define ERR_NONE           0
// Note: error codes -1..-79 are reserved standard C library errors with sign reverted.
//    these are defined in errno.h
#define ERR_BASE_RNC      -90

uint64_t LbSystemClockMilliseconds(void);

// Portable size_t formatting for printf-style macros
// Usage: ERRORLOG("size is %" PRIuSIZE " bytes", SZCAST(my_size))
#define PRIuSIZE "lu"
#define SZCAST(x) ((unsigned long)(x))

// Debug fuction-like macros - for free messages
#define ERRORMSG(format, ...) LbErrorLog(format "\n", ##__VA_ARGS__)
#define WARNMSG(format, ...) LbWarnLog(format "\n", ##__VA_ARGS__)
#define SYNCMSG(format, ...) LbSyncLog(format "\n", ##__VA_ARGS__)
#define JUSTMSG(format, ...) LbJustLog(format "\n", ##__VA_ARGS__)
#define SCRPTMSG(format, ...) LbScriptLog(format "\n", ##__VA_ARGS__)
#define NETMSG(format, ...) LbNetLog(format "\n", ##__VA_ARGS__)
#define NOMSG(format, ...)

// Debug function-like macros - for code logging (with function name)
#define ERRORLOG(format, ...) LbErrorLog("[%" PRIu32 "] %s: " format "\n", get_gameturn(), __func__ , ##__VA_ARGS__)
#define WARNLOG(format, ...) LbWarnLog("[%" PRIu32 "] %s: " format "\n", get_gameturn(), __func__ , ##__VA_ARGS__)
#define SYNCLOG(format, ...) LbSyncLog("[%" PRIu32 "] %s: " format "\n", get_gameturn(), __func__ , ##__VA_ARGS__)
#define JUSTLOG(format, ...) LbJustLog("[%" PRIu32 "] %s: " format "\n", get_gameturn(), __func__ , ##__VA_ARGS__)
extern TbBool detailed_multiplayer_logging;
// game_kind/GKind_MultiGame are kfx_sim_state (kfx_sim)-owned -- this macro
// only expands at call sites that already have kfx_sim_state.h visible
// (currently kfx_net only, which is ranked above kfx_sim), same as it
// previously required game_legacy.h visible for game.game_kind. See
// docs/refactor/stage-13-enforce-and-document.md.
#define MULTIPLAYER_LOG(format, ...) do { if (detailed_multiplayer_logging && kfx_sim_state.game_kind == GKind_MultiGame) { LbJustLog("[%" PRIu32 "][%" PRIu64 " ms] %s: " format "\n", get_gameturn(), LbSystemClockMilliseconds(), __func__ , ##__VA_ARGS__); } } while(0)
#define SCRPTLOG(format, ...) LbScriptLog(text_line_number,"%s: " format "\n", __func__ , ##__VA_ARGS__)
#define SCRPTERRLOG(format, ...) LbErrorLog("%s(line %lu): " format "\n", __func__ , text_line_number, ##__VA_ARGS__)
#define SCRPTWRNLOG(format, ...) LbWarnLog("%s(line %lu): " format "\n", __func__ , text_line_number, ##__VA_ARGS__)
#define CONFLOG(format, ...) LbConfigLog(text_line_number,"%s: " format "\n", __func__ , ##__VA_ARGS__)
#define CONFERRLOG(format, ...) LbErrorLog("%s(line %lu): " format "\n", __func__ , text_line_number, ##__VA_ARGS__)
#define CONFWRNLOG(format, ...) LbWarnLog("%s(line %lu): " format "\n", __func__ , text_line_number, ##__VA_ARGS__)
#define NETLOG(format, ...) LbNetLog("[%" PRIu32 "] %s: " format "\n", get_gameturn(), __func__ , ##__VA_ARGS__)
#define NOLOG(format, ...)

// Debug function-like macros - for debug code logging
#if (BFDEBUG_LEVEL > 0)
  #define SYNCDBG(dblv,format, ...) {\
    if (BFDEBUG_LEVEL > dblv)\
      LbSyncLog("%s: " format "\n", __func__ , ##__VA_ARGS__); }
  #define WARNDBG(dblv,format, ...) {\
    if (BFDEBUG_LEVEL > dblv)\
      LbWarnLog("%s: " format "\n", __func__ , ##__VA_ARGS__); }
  #define ERRORDBG(dblv,format, ...) {\
    if (BFDEBUG_LEVEL > dblv)\
      LbErrorLog("%s: " format "\n", __func__ , ##__VA_ARGS__); }
  #define NAVIDBG(dblv,format, ...) {\
    if (BFDEBUG_LEVEL > dblv)\
      LbNaviLog("%s: " format "\n", __func__ , ##__VA_ARGS__); }
  #define NETDBG(dblv,format, ...) {\
    if (BFDEBUG_LEVEL > dblv)\
      LbNetLog("%s: " format "\n", __func__ , ##__VA_ARGS__); }
  #define SCRIPTDBG(dblv,format, ...) {\
    if (BFDEBUG_LEVEL > dblv)\
      LbScriptLog(text_line_number,"%s: " format "\n", __func__ , ##__VA_ARGS__); }
#else
  #define SYNCDBG(dblv,format, ...)
  #define WARNDBG(dblv,format, ...)
  #define ERRORDBG(dblv,format, ...)
  #define NAVIDBG(dblv,format, ...)
  #define NETDBG(dblv,format, ...)
  #define SCRIPTDBG(dblv,format, ...)
#endif

#define MAX_TILES_X 170
#define MAX_TILES_Y 170
#define MAX_SUBTILES_X 511
#define MAX_SUBTILES_Y 511

enum AnglesAndDegrees {
    // Cardinal directions (clockwise from North)
    ANGLE_NORTH = 0,        // 0° - North direction (up)
    ANGLE_NORTHEAST = 256,  // 45° - Northeast direction (up-right)
    ANGLE_EAST = 512,       // 90° - East direction (right)
    ANGLE_SOUTHEAST = 768,  // 135° - Southeast direction (down-right)
    ANGLE_SOUTH = 1024,     // 180° - South direction (down)
    ANGLE_SOUTHWEST = 1280, // 225° - Southwest direction (down-left)
    ANGLE_WEST = 1536,      // 270° - West direction (left)
    ANGLE_NORTHWEST = 1792, // 315° - Northwest direction (up-left)
    ANGLE_MASK = 2047,      // Bitmask for angle/degrees values (0x7FF)
    // Degrees
    DEGREES_2_8125 = 16,    // 2.8125° - DEGREES_180 / 64
    DEGREES_8_18 = 46,      // 8.18° - DEGREES_180 / 22
    DEGREES_10 = 56,        // 10° - DEGREES_180 / 18
    DEGREES_11_25 = 64,     // 11.25° - DEGREES_180 / 16
    DEGREES_15 = 85,        // 15° - DEGREES_180 / 12
    DEGREES_20 = 113,       // 20° - DEGREES_180 / 9
    DEGREES_22_5 = 128,     // 22.5° - DEGREES_180 / 8
    DEGREES_30 = 170,       // 30° - DEGREES_180 / 6
    DEGREES_45 = 256,       // 45° - DEGREES_180 / 4
    DEGREES_50 = 284,       // 50°
    DEGREES_60 = 341,       // 60° - DEGREES_180 / 3
    DEGREES_90 = 512,       // 90° - DEGREES_180 / 2
    DEGREES_120 = 682,      // 120° - 2 * DEGREES_180 / 3
    DEGREES_135 = 768,      // 135°
    DEGREES_180 = 1024,     // 180° - Half a circle
    DEGREES_202_5 = 1151,   // 202.5° - Sprite flip threshold
    DEGREES_225 = 1280,     // 225°
    DEGREES_270 = 1536,     // 270°
    DEGREES_315 = 1792,     // 315°
    DEGREES_337_5 = 1919,   // 337.5° - Sprite flip threshold
    DEGREES_360 = 2048,     // 360° - Full circle
};

#pragma pack(1)

/** Screen coordinate in scale of the game (resolution independent). */
typedef int32_t ScreenCoord;
/** Screen coordinate in scale of the real screen. */
typedef int32_t RealScreenCoord;
/** Player identification number, or owner of in-game thing/room/slab. */
typedef int8_t PlayerNumber;
/** bitflags where each bit represents a player (e.g. player id 0 = 0b000001, player id 1 = 0b000010, player id 2 = 0b000100). */
typedef uint16_t PlayerBitFlags;
/** Type which stores thing class. */
typedef uint8_t ThingClass;
/** Type which stores thing model. */
typedef int16_t ThingModel;
/** Type which stores thing index. */
typedef uint16_t ThingIndex;
/** Type which stores effectModels on positive or EffectElements on Negative. Should be as big as ThingModel */
typedef int16_t EffectOrEffElModel;
/** Type which stores creature state index. */
typedef uint16_t CrtrStateId;
/** Type which stores creature experience level. */
typedef uint8_t CrtrExpLevel;
/** Type which stores keeper power level. */
typedef uint8_t KeepPwrLevel;
/** Type which stores creature annoyance reason, from CreatureAngerReasons enumeration. */
typedef uint8_t AnnoyMotive;
/** Type which stores room kind index. */
typedef uint8_t RoomKind;

// Moved from kfx_sim's room_data.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- pure ID vocabulary for room
// kinds, used across every library including kfx_config's
// config_terrain.c.
//don't use any of these in new code, everything should be done trough config
enum RoomKinds {
    RoK_NONE                =   0,
    RoK_ENTRANCE            =   1,
    RoK_TREASURE            =   2,
    RoK_LIBRARY             =   3,
    RoK_PRISON              =   4,
    RoK_TORTURE             =   5,
    RoK_TRAINING            =   6,
    RoK_DUNGHEART           =   7,
    RoK_WORKSHOP            =   8,
    RoK_SCAVENGER           =   9,
    RoK_TEMPLE              =  10,
    RoK_GRAVEYARD           =  11,
    RoK_BARRACKS            =  12,
    RoK_GARDEN              =  13,
    RoK_LAIR                =  14,
    RoK_BRIDGE              =  15,
    RoK_GUARDPOST           =  16,
    RoK_TYPES_COUNT         =  17,
    RoK_SELL                = 255,
};

/** Type which stores room role flags. */
typedef uint32_t RoomRole;
/** Type which stores room index. */
typedef uint16_t RoomIndex;
/** Type which stores slab kind index. */
typedef uint8_t SlabKind;

// Moved from kfx_sim's slab_data.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- pure ID vocabulary for slab
// types, used across every library including kfx_config's
// config_terrain.c.
enum SlabTypes {
    SlbT_ROCK               =   0,
    SlbT_GOLD               =   1,
    SlbT_EARTH              =   2,
    SlbT_TORCHDIRT          =   3,
    SlbT_WALLDRAPE          =   4,
    SlbT_WALLTORCH          =   5,
    SlbT_WALLWTWINS         =   6,
    SlbT_WALLWWOMAN         =   7,
    SlbT_WALLPAIRSHR        =   8,
    SlbT_DAMAGEDWALL        =   9,
    SlbT_PATH               =  10,
    SlbT_CLAIMED            =  11,
    SlbT_LAVA               =  12,
    SlbT_WATER              =  13,
    SlbT_ENTRANCE           =  14,
    SlbT_ENTRANCE_WALL      =  15,
    SlbT_TREASURE           =  16,
    SlbT_TREASURE_WALL      =  17,
    SlbT_LIBRARY            =  18,
    SlbT_LIBRARY_WALL       =  19,
    SlbT_PRISON             =  20,
    SlbT_PRISON_WALL        =  21,
    SlbT_TORTURE            =  22,
    SlbT_TORTURE_WALL       =  23,
    SlbT_TRAINING           =  24,
    SlbT_TRAINING_WALL      =  25,
    SlbT_DUNGHEART          =  26,
    SlbT_DUNGHEART_WALL     =  27,
    SlbT_WORKSHOP           =  28,
    SlbT_WORKSHOP_WALL      =  29,
    SlbT_SCAVENGER          =  30,
    SlbT_SCAVENGER_WALL     =  31,
    SlbT_TEMPLE             =  32,
    SlbT_TEMPLE_WALL        =  33,
    SlbT_GRAVEYARD          =  34,
    SlbT_GRAVEYARD_WALL     =  35,
    SlbT_GARDEN             =  36,
    SlbT_GARDEN_WALL        =  37,
    SlbT_LAIR               =  38,
    SlbT_LAIR_WALL          =  39,
    SlbT_BARRACKS           =  40,
    SlbT_BARRACKS_WALL      =  41,
    SlbT_DOORWOOD1          =  42,
    SlbT_DOORWOOD2          =  43,
    SlbT_DOORBRACE1         =  44,
    SlbT_DOORBRACE2         =  45,
    SlbT_DOORIRON1          =  46,
    SlbT_DOORIRON2          =  47,
    SlbT_DOORMAGIC1         =  48,
    SlbT_DOORMAGIC2         =  49,
    SlbT_SLAB50             =  50, //Has special properties and is known as slab50 until a better name is found
    SlbT_BRIDGE             =  51,
    SlbT_GEMS               =  52,
    SlbT_GUARDPOST          =  53,
    SlbT_PURPLE             =  54,
    SlbT_DOORSECRET1        =  55,
    SlbT_DOORSECRET2        =  56,
    SlbT_ROCK_FLOOR         =  57,
    SlbT_DOORMIDAS1         =  58,
    SlbT_DOORMIDAS2         =  59,
    SlbT_DENSEGOLD          =  60,
};

// Moved from kfx_sim's slab_data.h (Abyss dungeons, #5169) -- same
// pure-ID-vocabulary situation as enum SlabTypes above: kfx_config's
// config_terrain.c (slab_kind_from_wlb_type/slab_kind_is_bridgeable)
// needs these names, and kfx_config sits below kfx_sim.
enum WlbType {
    WlbT_None   = 0,
    WlbT_Lava   = 1,
    WlbT_Water  = 2,
    WlbT_Bridge = 3,
    WlbT_Abyss  = 4,
};

/** Type which stores spell kind index. */
typedef uint16_t SpellKind;
/** Type which stores PwrK_* values. */
typedef uint16_t PowerKind;
/** Type which stores EvKind_* values. */
typedef uint8_t EventKind;

// Moved from kfx_config's config.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- pure ID vocabulary for file
// path resolution, zero functional coupling, already used by every
// library including kfx_platform itself (bflib_sndlib.cpp).
enum TbFileGroups {
        FGrp_None,
        FGrp_StdData,
        FGrp_LrgData,
        FGrp_FxData,
        FGrp_LoData,
        FGrp_HiData,
        FGrp_VarLevels,
        FGrp_Save,
        FGrp_SShots,
        FGrp_StdSound,
        FGrp_LrgSound,
        FGrp_AtlSound,
        FGrp_Main,
        FGrp_Campgn,
        FGrp_CmpgLvls,
        FGrp_LandView,
        FGrp_CrtrData,
        FGrp_CmpgCrtrs,
        FGrp_CmpgConfig,
        FGrp_CmpgMedia,
        FGrp_Music,
        FGrp_MpLevels,
};

// Moved from kfx_sim's map_events.h (stage 13.3) -- pure name/ID
// vocabulary for map event kinds; config_creature.c needs it for its
// NamedCommand table.
enum EventKinds {
    EvKind_Nothing = 0,
    EvKind_HeartAttacked,
    EvKind_EnemyFight,
    EvKind_Objective,
    EvKind_Breach,
    EvKind_NewRoomResrch,
    EvKind_NewCreature,
    EvKind_NewSpellResrch,
    EvKind_NewTrap,
    EvKind_NewDoor,
    EvKind_CreatrScavenged,
    EvKind_TreasureRoomFull,
    EvKind_CreaturePayday,
    EvKind_AreaDiscovered,
    EvKind_SpellPickedUp,
    EvKind_RoomTakenOver,
    EvKind_CreatrIsAnnoyed,
    EvKind_NoMoreLivingSet,
    EvKind_AlarmTriggered,
    EvKind_RoomUnderAttack,
    EvKind_NeedTreasureRoom,
    EvKind_Information,
    EvKind_RoomLost,
    EvKind_CreatrHungry,
    EvKind_TrapCrateFound,
    EvKind_DoorCrateFound,
    EvKind_DnSpecialFound,
    EvKind_QuickInformation,
    EvKind_FriendlyFight,
    EvKind_WorkRoomUnreachable,
    EvKind_StorageRoomUnreachable,
    EvKind_PrisonerStarving,
    EvKind_TorturedHurt,
    EvKind_EnemyDoor,
    EvKind_SecretDoorDiscovered,
    EvKind_SecretDoorSpotted,
};
/** Type which stores dungeon special kind. */
typedef uint16_t SpecialKind;
/** Type which stores index of the new event, or negative index of updated event, in map events array. */
typedef uint8_t EventIndex;
typedef uint8_t BattleIndex;
typedef int32_t HitPoints;
/** Type which stores TUFRet_* values. */
typedef int16_t TngUpdateRet;
/** Type which stores CrStRet_* values. */
typedef int16_t CrStateRet;
/** Type which stores CrCkRet_* values. */
typedef int16_t CrCheckRet;
/** Type which stores Job_* values. */
typedef uint64_t CreatureJob;
/** Creature instance index, stores CrInst_* values. */
typedef int16_t CrInstance;
/** Creature attack type, stores AttckT_* values. */
typedef int16_t CrAttackType;
/** Creature death flags, stores CrDed_* values. */
typedef uint16_t CrDeathFlags;
/** Level number within a campaign. */
typedef int32_t LevelNumber;
/** Game turn number, used for in-game time computations. */
typedef uint32_t GameTurn;
/** Game turns difference, used for in-game time computations. */
typedef int32_t GameTurnDelta;
/** Identifier of a national text string. */
typedef int32_t TextStringId;
/** Map coordinate in full resolution. Position within subtile is scaled 0..255. */
typedef int32_t MapCoord;
/** Distance between map coordinates in full resolution. */
typedef int32_t MapCoordDelta;
/** Map subtile coordinate. Every slab consists of 3x3 subtiles. */
typedef int32_t MapSubtlCoord;
/** Distance between map subtiles. */
typedef int32_t MapSubtlDelta;
/** Map slab coordinate. Slab is a cubic part of map with specific content. */
typedef int16_t MapSlabCoord;
/** Distance between map coordinates in slabs.  */
typedef int16_t MapSlabDelta;
/** Map subtile 2D coordinates, coded into one number. */
typedef int32_t SubtlCodedCoords;
/** Map slab 2D coordinates, coded into one number. */
typedef uint32_t SlabCodedCoords;
/** Index in the columns array. */
typedef int16_t ColumnIndex;
/** Movement speed on objects in the game. */
typedef int16_t MoveSpeed;
/** Parameter for storing gold sum or price. */
typedef int32_t GoldAmount;
/** Type for storing Action Point index.
 * Note that it stores index in array, not Action Point number. */
typedef int32_t ActionPointId;
/** Not to be confused with ActionPointId */
typedef uint16_t ActionPointNumber;
/** Parameter for filtering functions which return an item with max filter parameter. */
typedef int32_t FilterParam;
/** Type which stores IAvail_* values. */
typedef int8_t ItemAvailability;
/** Type which stores hit filters for things as THit_* values. */
typedef uint8_t ThingHitType;
/** Type which stores hit filters for things as HitTF_* flags. */
typedef uint64_t HitTargetFlags;
/** Index within active_buttons[] array. */
typedef int8_t ActiveButtonID;
/** Type which stores FeST_* values from FrontendMenuStates enumeration. */
typedef int16_t FrontendMenuState;
/** Type which stores digger task type as DigTsk_* values. */
typedef uint16_t SpDiggerTaskType;
/** Flags for tracing route for creature movement. */
typedef uint8_t NaviRouteFlags;
/** data used for navigating contains floor height, locked doors per player, unsafe surfaces */
typedef uint16_t NavColour;
/** Either North (0), East (1), South (2), or West (3). */
typedef int8_t SmallAroundIndex;
/** a player state as defined in config_players*/
typedef uint8_t PlayerState;
/** Index to the Creature Control array. */
typedef uint16_t CctrlIndex;
/** index to a function, positive for C functions, negative for lua functions*/
typedef int16_t FuncIdx;
/** locations like an action point, last event etc. */
typedef uint32_t TbMapLocation;
/** Controller buttons state. flags field, each bit represents a button */
typedef uint64_t TbControllerButtons; 

/**
 * Stores a 2d coordinate (x,y).
 *
 * Members:
 * .val - coord position (relative to whole map)
 * .stl.pos - coord position (relative to subtile)
 * .stl.num - subtile position (relative to whole map)
 */
struct Coord2d {
    union { // x position
      int32_t val; /**< x.val - coord x position (relative to whole map) */
      struct { // subtile
        uint8_t pos; /**< x.stl.pos - coord x position (relative to subtile) */
        uint16_t num; /**< x.stl.num - subtile x position (relative to whole map) */
        } stl;
    } x;
    union { // y position
      int32_t val; /**< y.val - coord y position (relative to whole map) */
      struct { // subtile
        uint8_t pos; /**< y.stl.pos - coord y position (relative to subtile) */
        uint16_t num; /**< y.stl.num - subtile y position (relative to whole map) */
        } stl;
    } y;
};

/**
 * Stores a 3d coordinate (x,y).
 *
 * Members:
 * .val - coord position (relative to whole map)
 * .stl.pos - coord position (relative to subtile)
 * .stl.num - subtile position (relative to whole map)
 */
struct Coord3d {
    union { // x position
      int32_t val; /**< x.val - coord x position (relative to whole map) */
      struct { // subtile
        uint8_t pos; /**< x.stl.pos - coord x position (relative to subtile) */
        uint16_t num; /**< x.stl.num - subtile x position (relative to whole map) */
        } stl;
    } x;
    union { // y position
      int32_t val; /**< y.val - coord y position (relative to whole map) */
      struct { // subtile
        uint8_t pos; // y.stl.pos - coord y position (relative to subtile) */
        uint16_t num; // y.stl.num - subtile y position (relative to whole map) */
        } stl;
    } y;
    union { // z position
      int32_t val; /**< z.val - coord z position (relative to whole map) */
      struct { // subtile
        uint8_t pos; /**< z.stl.pos - coord z position (relative to subtile) */
        uint16_t num; /**< z.stl.num - subtile z position (relative to whole map) */
        } stl;
    } z;
};

struct CoordDelta3d {
    union {
      int32_t val;
      struct {
        uint8_t pos;
        int16_t num;
        } stl;
    } x;
    union {
      int32_t val;
      struct {
        uint8_t pos;
        int16_t num;
        } stl;
    } y;
    union {
      int32_t val;
      struct {
        uint8_t pos;
        int16_t num;
        } stl;
    } z;
};

// Moved from src/engine_camera.h (stage 7 prep) -- needed by kfx_config
// (config_trapdoor.h's shotvector field), kfx_sim, and kfx_script, so it
// must live at the lowest layer all of those can reach.
struct ComponentVector {
    short x;
    short y;
    short z;
};

struct Around { // sizeof = 2
  int8_t delta_x;
  int8_t delta_y;
};

struct AroundLByte {
  int16_t delta_x;
  int16_t delta_y;
};

#pragma pack()

// Moved from src/kfx_sim/include/thing_navigate.h (stage-06a-ariadne-
// pathfinding-interface.md physical split) -- pure bit-flag constants (no
// state), used by both kfx_sim (thing_physics.c, thing_navigate.c, ...) and
// kfx_pathfinding (ariadne.c, ariadne_wallhug.c), so it must live at the
// lowest layer both can reach.
enum SlabBlockedFlags {
    SlbBloF_None    = 0x00,
    SlbBloF_WalledX = 0x01,
    SlbBloF_WalledY = 0x02,
    SlbBloF_WalledZ = 0x04,
};

// Moved from src/kfx_sim/include/map_data.h (stage-06a-ariadne-pathfinding-
// interface.md physical split) -- pure, zero-state subtile/slab/coordinate
// math, used bare throughout kfx_sim and now also kfx_pathfinding
// (ariadne_wallhug.c), so it must live at the lowest layer both can reach.
#define STL_PER_SLB 3
#define COORD_PER_STL 256
#define COORD_PER_SLB (COORD_PER_STL*STL_PER_SLB)
#define subtile_slab(stl) ((stl)/STL_PER_SLB)
#define slab_subtile(slb,subnum) ((MapSubtlCoord)(slb)*STL_PER_SLB+(MapSubtlCoord)(subnum))
#define slab_subtile_center(slb) ((MapSubtlCoord)(slb)*STL_PER_SLB+(MapSubtlCoord)1)
#define coord_subtile(coord) ((coord)/COORD_PER_STL)
#define coord_slab(coord) ((coord)/(COORD_PER_SLB))
#define subtile_coord(stl,spos) ((stl)*COORD_PER_STL+(spos))
#define slab_coord(slb) ((slb) * (COORD_PER_SLB))
#define subtile_coord_center(stl) ((stl)*COORD_PER_STL+COORD_PER_STL/2)

struct IPOINT_2D {
    int32_t x;
    int32_t y;
};

struct IPOINT_3D {
    int32_t x;
    int32_t y;
    int32_t z;
};

struct UPOINT_2D {
    uint32_t x;
    uint32_t y;
};

struct UPOINT_3D {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct USPOINT_2D {
    uint16_t x;
    uint16_t y;
};

struct IRECT_2D {
    int32_t l;
    int32_t r;
    int32_t t;
    int32_t b;
};

struct PickedUpOffset
{
    int16_t delta_x;
    int16_t delta_y;
};

// Moved here from gui_topmsg.h (stage 6, docs/refactor/stage-06-kfx-sim.md)
// so kfx_sim code can report allocation/pathing exhaustion via
// sim_feedback->report_error_stat() without depending on the frontend
// header that implements the on-screen display.
enum ErrorStatisticEntries {
    ESE_NoFreeThings = 0,
    ESE_NoFreeCreatrs,
    ESE_NoFreeTriangls,
    ESE_NoFreeRooms,
    ESE_BadCreatrState,
    ESE_NoFreePathPts,
    ESE_BadPathHeap,
    ESE_BadRouteTree,
    ESE_CantReadPackets,
    ESE_NoFreeUnsyncedThings,
};

// Moved here from gui_soundmsgs.h (stage 6) for the same reason as
// ErrorStatisticEntries above -- kfx_sim reports speech-message triggers
// via sim_feedback->play_sound_message() and needs the SMsg_* vocabulary
// without depending on gui_soundmsgs.h itself.
enum TbSpeechMessages {
    SMsg_None = 0,
    SMsg_CreatrAngryAnyReason,
    SMsg_CreatrAngryNoLair,
    SMsg_CreatrAngryNotPaid,
    SMsg_CreatrAngryNoFood,
    SMsg_CreatrDestroyRooms,
    SMsg_CreatureLeaving,
    SMsg_WallsBreach,
    SMsg_HeartUnderAttack,
    SMsg_BattleDefeat,
    SMsg_BattleVictory, // 10
    SMsg_BattleDeath,
    SMsg_BattleWon,
    SMsg_CreatureDefending,
    SMsg_CreatureAttacking,
    SMsg_EnemyDestroyRooms,
    SMsg_EnemyClaimGround,
    SMsg_EnemyRoomTakeOver,
    SMsg_NewRoomTakenOver,
    SMsg_LordOfLandComming,
    SMsg_FingthingFriends, // 20
    SMsg_BattleOver,
    SMsg_GardenTooSmall,
    SMsg_LairTooSmall,
    SMsg_TreasuryTooSmall,    //'You need a bigger treasure room'
    SMsg_LibraryTooSmall,
    SMsg_PrisonTooSmall,
    SMsg_TortureTooSmall,
    SMsg_TrainingTooSmall,
    SMsg_WorkshopTooSmall,
    SMsg_ScavengeTooSmall, // 30
    SMsg_TempleTooSmall,
    SMsg_GraveyardTooSmall,
    SMsg_BarracksTooSmall,
    SMsg_NoRouteToGarden,
    SMsg_NoRouteToTreasury,    //'Some of your minions are unable to reach the treasure room'
    SMsg_NoRouteToLair,
    SMsg_EntranceClaimed,
    SMsg_EntranceLost,
    SMsg_RoomTreasrNeeded,    //'You must build a treasure room, to store gold'
    SMsg_RoomLairNeeded, // 40
    SMsg_RoomGardenNeeded,
    SMsg_ResearchedRoom,
    SMsg_ResearchedSpell,
    SMsg_ManufacturedDoor,
    SMsg_ManufacturedTrap,
    SMsg_NoMoreReseach,
    SMsg_SpellbookTaken,
    SMsg_TrapTaken,
    SMsg_DoorTaken,
    SMsg_SpellbookStolen, // 50
    SMsg_TrapStolen,
    SMsg_DoorStolen,
    SMsg_TortureInformation,
    SMsg_TortureConverted,
    SMsg_PrisonMadeSkeleton,
    SMsg_TortureMadeGhost,
    SMsg_PrisonersEscaping,
    SMsg_GraveyardMadeVampire,
    SMsg_CreatrFreedPrison,
    SMsg_PrisonersStarving, // 60
    SMsg_CreatureScanvenged,
    SMsg_MinionScanvenged,
    SMsg_CreatureJoinedEnemy,
    SMsg_CreatureRevealInfo,
    SMsg_SacrificeGood,
    SMsg_SacrificeReward,
    SMsg_SacrificeNeutral,
    SMsg_SacrificeBad,
    SMsg_SacrificePunish,
    SMsg_SacrificeWishing, // 70
    SMsg_DiscoveredSpecial,
    SMsg_DiscoveredSpell,
    SMsg_DiscoveredDoor,
    SMsg_DiscoveredTrap,
    SMsg_CreaturesJoinedYou,
    SMsg_DugIntoNewArea,
    SMsg_SpecRevealMap,
    SMsg_SpecResurrect,
    SMsg_SpecTransfer,
    SMsg_CommonAcknowledge, // 80
    SMsg_SpecHeroStolen,
    SMsg_SpecCreatrDoubled,
    SMsg_SpecIncLevel,
    SMsg_SpecWallsFortify,
    SMsg_SpecHiddenWorld,
    SMsg_GoldLow,
    SMsg_GoldNotEnough,     //'You do not have enough gold'
    SMsg_NoGoldToScavenge,
    SMsg_NoGoldToTrain,
    SMsg_Payday, // 90
    SMsg_FullOfPies,
    SMsg_SurrealHappen,
    SMsg_StrangeAccent,
    SMsg_PantsTooTight,    //'Your pants are definitely too tight'
    SMsg_CraveChocolate,
    SMsg_SmellAgain,
    SMsg_Hello,
    SMsg_Glaagh,
    SMsg_Achew,
    SMsg_Chgreche,        // 100
    SMsg_WorkerJobsLimit, //'You cannot give your Imps any more jobs'
    SMsg_GameLoaded,      //'Game loaded'
    SMsg_GameSaved,
    SMsg_DefeatedKeeper,
    SMsg_LevelFailed,     //'You have been defeated'
    SMsg_LevelWon,        //'Your have conquered this realm'
    SMsg_SenceAvatar,     //'I sense the presence of the Avatar'
    SMsg_AvatarBodyVanish,
    SMsg_GameFinalVictory,
    SMsg_MAX = 126,
};

// Moved here from gui_soundmsgs.h alongside TbSpeechMessages, same reason:
// kfx_sim's many output_message()/output_room_message() call sites need
// these without depending on the frontend header that implements them.
#define SMsg_FunnyMessages      SMsg_FullOfPies  // Starts a list of 10 funny quotes
#define SMsg_EnemyHarassments  110  // Starts a list of harassments
#define SMsg_EnemyLordQuote    118  // Starts a list of lord of the land quotes

// Define empty messages, which may be used later
#define SMsg_NoRouteToPrison SMsg_None
#define SMsg_NoRouteToGraveyard SMsg_None

// Moved here from gui_soundmsgs.h, same reason: widely used as
// output_message()/output_room_message() duration arguments outside
// kfx_frontend.
#define MESSAGE_DURATION_ROOM_NEED     500
#define MESSAGE_DURATION_ROOM_SMALL    500
#define MESSAGE_DURATION_WORSHOP_FULL 1750
#define MESSAGE_DURATION_TREASURY      500
#define MESSAGE_DURATION_FIGHT         400
#define MESSAGE_DURATION_BATTLE         40
#define MESSAGE_DURATION_CRTR_MOOD     500
#define MESSAGE_DURATION_SPECIAL       100
#define MESSAGE_DURATION_LORD          100
#define MESSAGE_DURATION_KEEPR_TAUNT   500
#define MESSAGE_DURATION_CRTR_JOINED   500
#define MESSAGE_DURATION_STARVING      500

// Moved here from gui_soundmsgs.h alongside TbSpeechMessages, same reason.
enum OutputMessageKinds {
    OMsg_None = 0,
    OMsg_RoomNeeded,
    OMsg_RoomTooSmall,
    OMsg_RoomNoRoute,
    OMsg_RoomFull,
};

typedef unsigned int OutputMessageKind;

// Moved here from gui_msgs.h, same reason as the enums above.
enum MessageTypes {
    MsgType_Player = 0,
    MsgType_Creature,
    MsgType_CreatureSpell,
    MsgType_Room,
    MsgType_KeeperSpell,
    MsgType_Query, //5
    MsgType_Blank,
    MsgType_CreatureInstance,
    MsgType_Custom,
};

// Moved here from gui_frontmenu.h (stage 9, docs/refactor/stage-09-kfx-game.md)
// -- kfx_game callers (lvl_script_commands.c) need the GMnu_* menu-ID
// vocabulary to pass through game_callbacks->is_menu_active()/
// turn_on_ingame_menu()/turn_off_ingame_menu(), without depending on
// gui_frontmenu.h's menu-management functions (kfx_frontend, above
// kfx_game). Same "small vocabulary enum" treatment as MessageTypes above.
enum GUI_Menus {
  GMnu_MAIN               =  1,
  GMnu_ROOM               =  2,
  GMnu_SPELL              =  3,
  GMnu_TRAP               =  4,
  GMnu_CREATURE           =  5,
  GMnu_EVENT              =  6,
  GMnu_QUERY              =  7,
  GMnu_OPTIONS            =  8,
  GMnu_INSTANCE           =  9,
  GMnu_QUIT               = 10,
  GMnu_LOAD               = 11,
  GMnu_SAVE               = 12,
  GMnu_VIDEO              = 13,
  GMnu_SOUND              = 14,
  GMnu_ERROR_BOX          = 15,
  GMnu_TEXT_INFO          = 16,
  GMnu_HOLD_AUDIENCE      = 17,
  GMnu_FEMAIN             = 18,
  GMnu_FELOAD             = 19,
  GMnu_FENET_SERVICE      = 20,
  GMnu_FENET_SESSION      = 21,
  GMnu_FENET_START        = 22,
  GMnu_FESTATISTICS       = 25,
  GMnu_FEHIGH_SCORE_TABLE = 26,
  GMnu_DUNGEON_SPECIAL    = 27,
  GMnu_RESURRECT_CREATURE = 28,
  GMnu_TRANSFER_CREATURE  = 29,
  GMnu_ARMAGEDDON         = 30,
  GMnu_CREATURE_QUERY1    = 31,
  GMnu_CREATURE_QUERY3    = 32,
  GMnu_CREATURE_QUERY4    = 33,
  GMnu_BATTLE             = 34,
  GMnu_CREATURE_QUERY2    = 35,
  GMnu_FEDEFINE_KEYS      = 36,
  GMnu_AUTOPILOT          = 37,
  GMnu_SPELL_LOST         = 38,
  GMnu_FEOPTION           = 39,
  GMnu_FELEVEL_SELECT     = 40,
  GMnu_FECAMPAIGN_SELECT  = 41,
  GMnu_FEERROR_BOX        = 42,
  GMnu_FEADD_SESSION      = 43,
  GMnu_MAPPACK_SELECT     = 44,
  GMnu_MSG_BOX            = 45,
  GMnu_SPELL2             = 46,
  GMnu_ROOM2              = 47,
  GMnu_TRAP2              = 48,
  GMnu_MP_MAPPACK_SELECT  = 49,
};

#define MENU_INVALID_ID -1

// Moved here from frontmenu_ingame_tabs.h (stage 9,
// docs/refactor/stage-09-kfx-game.md) -- kfx_game callers
// (lvl_script_commands.c's gui_button_group_desc[] table) need the
// GID_* button-group-ID vocabulary without frontmenu_ingame_tabs.h's
// menu/button-drawing functions. Same "small vocabulary enum" treatment
// as GUI_Menus above.
enum IngameButtonGroupIDs {
    GID_NONE = 0,
    GID_MINIMAP_AREA,
    GID_TABS_AREA,
    GID_INFO_PANE,
    GID_ROOM_PANE,
    GID_POWER_PANE,
    GID_TRAP_PANE,
    GID_DOOR_PANE,
    GID_CREATR_PANE,
    GID_MESSAGE_AREA,
};

/**
 * Type to store GMnu_* items from GUI_Menus enumeration.
 */
typedef long MenuID;

// Moved here from kfx_sim's thing_effects.h (stage 13,
// docs/refactor/stage-13-enforce-and-document.md) -- config_effects.c/
// config_magic.c/config_trapdoor.c need this name/ID vocabulary to
// populate NamedCommand config-parsing tables, without depending on
// thing_effects.h's effect-simulation functions (kfx_sim, above
// kfx_config). Same "small vocabulary enum" treatment as GUI_Menus/
// IngameButtonGroupIDs above.
enum ThingHitTypes {
    THit_None = 0,
    THit_CrtrsNObjcts, // Affect all creatures and all objects
    THit_CrtrsOnly, // Affect only creatures
    THit_CrtrsNObjctsNotOwn, // Affect not own creatures and objects
    THit_CrtrsOnlyNotOwn, // Affect not own creatures
    THit_CrtrsNotArmourNotOwn, // Affect not own creatures which are not protected by Armour spell
    THit_All, // Affect all things
    THit_HeartOnly, // Affect only dungeon hearts
    THit_HeartOnlyNotOwn, // Affect only not own dungeon hearts
    THit_CrtrsNObjctsNShot, // Affect all creatures and all objects, also allow colliding with other shots
    THit_TrapsAll, // Affect all traps, not just the ones that are destructable
    THit_CrtrsOnlyOwn, // Affect only own creatures
    THit_TypesCount, // Last item in enumeration, allows checking amount of valid types
};

enum ThingEffectKind {
    TngEff_None = 0,
    TngEff_Explosion1, // expl small
    TngEff_Explosion2,
    TngEff_Explosion3,
    TngEff_Explosion4,
    TngEff_Explosion5, // expl big
    TngEff_HitBleedingUnit,
    TngEff_ChickenBlood,
    TngEff_Blood3,
    TngEff_Blood4,
    TngEff_Blood5, // blood big
    TngEff_Gas1, // fart
    TngEff_Gas2, // fart
    TngEff_Gas3, // fart
    TngEff_WoPExplosion,
    TngEff_IceShard, // Ice shard
    TngEff_HarmlessGas1, // fart without damage
    TngEff_HarmlessGas2, // fart without damage
    TngEff_HarmlessGas3, // fart without damage
    TngEff_Drip1, // water drip
    TngEff_Drip2,
    TngEff_Drip3,
    TngEff_HitFrozenUnit,
    TngEff_Hail,
    TngEff_DeathIceExplosion,
    TngEff_RockChips, // less dirt
    TngEff_DirtRubble,
    TngEff_DirtRubbleBig, // more dirt
    TngEff_ImpSpangleRed,
    TngEff_Drip4, // ice drip?
    TngEff_Cloud, // super long cloud?
    TngEff_HarmlessGas4, // super wide cloud?
    TngEff_GoldRubble1, // small gold coins
    TngEff_GoldRubble2,
    TngEff_GoldRubble3, // big gold chunks
    TngEff_TempleSplash,
    TngEff_CeilingBreach,
    TngEff_StrangeGas1, // strange gas
    TngEff_StrangeGas2, // strange gas
    TngEff_StrangeGas3, // strange gas
    TngEff_DecelerationGas1, // fart slow
    TngEff_DecelerationGas2,
    TngEff_DecelerationGas3,
    TngEff_Eruption,
    TngEff_HearthCollapse,
    TngEff_Explosion6, // claim with sound???
    TngEff_SpangleRedBig,
    TngEff_ColouredRingOfFire, // spiral fx
    TngEff_Flash, // flash with whiteout
    TngEff_Dummy,
    TngEff_Explosion7, // temple? explosion with sound
    TngEff_FeatherPuff,
    TngEff_Explosion8,
    TngEff_ResearchComplete,
    TngEff_RoomSparkeSmall,
    TngEff_RoomSparkeMedium,
    TngEff_RoomSparkeLarge,
    TngEff_ImpSpangleBlue,
    TngEff_ImpSpangleGreen,
    TngEff_ImpSpangleYellow,
    TngEff_BallPuffRed, // teleport puff red
    TngEff_BallPuffBlue,
    TngEff_BallPuffGreen,
    TngEff_BallPuffYellow, // teleport puff yellow
    TngEff_BallPuffWhite, // teleport puff white
    TngEff_BloodyFootstep,
    TngEff_Blood7, // blood splat
    TngEff_SpecialBox,
    TngEff_BoulderSink, // boulder sink
    TngEff_ImpSpangleWhite,
    TngEff_ImpSpanglePurple,
    TngEff_BallPuffPurple,
    TngEff_ImpSpangleBlack,
    TngEff_BallPuffBlack,
    TngEff_ImpSpangleOrange,
    TngEff_BallPuffOrange,
    TngEff_FallingIceBlocks,
    TngEff_SlowKeeperPower,
    TngEff_TinySparks,
    TngEff_CoinFountain,
    TngEff_FearCircle,
    TngEff_CrazyGas,
};

enum ThingEffectElements {
    TngEffElm_None = 0,
    TngEffElm_Blast1,
    TngEffElm_Blood1,
    TngEffElm_Blood2,
    TngEffElm_Blood3,
    TngEffElm_Unknown05,
    TngEffElm_SpikedBall,
    TngEffElm_Cloud1,
    TngEffElm_SmallSparkles,
    TngEffElm_BallOfLight,
    TngEffElm_RedFlameBig, // 10
    TngEffElm_IceShard,
    TngEffElm_Leaves1,
    TngEffElm_Thingy2,
    TngEffElm_Thingy3,
    TngEffElm_TinyFlash1,
    TngEffElm_FlashBall1,
    TngEffElm_RedFlash,
    TngEffElm_FlashBall2,
    TngEffElm_TinyFlash2,
    TngEffElm_PurpleStars, // 20
    TngEffElm_Cloud2,
    TngEffElm_Drip1,
    TngEffElm_Blood4,
    TngEffElm_IceMelt1,
    TngEffElm_IceMelt2,
    TngEffElm_TinyRock,
    TngEffElm_MedRock,
    TngEffElm_LargeRock1,
    TngEffElm_Drip2,
    TngEffElm_LavaFlameStationary, // 30
    TngEffElm_Waterdrop,
    TngEffElm_LavaFlameMoving,
    TngEffElm_LargeRock2,
    TngEffElm_Unknown34,
    TngEffElm_Unknown35,
    TngEffElm_Unknown36,
    TngEffElm_EntranceMist,
    TngEffElm_Splash,
    TngEffElm_Blast2,
    TngEffElm_Drip3, // 40
    TngEffElm_Price,
    TngEffElm_ElectricBall1,
    TngEffElm_RedTwinkle,
    TngEffElm_RedTwinkle2,
    TngEffElm_Heal,
    TngEffElm_Unknown46,
    TngEffElm_Cloud3,
    TngEffElm_LargeRock3,
    TngEffElm_Gold1,
    TngEffElm_Gold2, // 50
    TngEffElm_Gold3,
    TngEffElm_Flash,
    TngEffElm_ElectricBall2,
    TngEffElm_RedPuff,
    TngEffElm_RedFlame,
    TngEffElm_BlueFlame,
    TngEffElm_GreenFlame,
    TngEffElm_YellowFlame,
    TngEffElm_Chicken,
    TngEffElm_ElectricBall3, // 60
    TngEffElm_Feathers,
    TngEffElm_Unknown62,
    TngEffElm_WhiteSparklesSmall,
    TngEffElm_GreenSparklesSmall,
    TngEffElm_RedSparklesSmall,
    TngEffElm_BlueSparklesSmall,
    TngEffElm_WhiteSparklesMed,
    TngEffElm_GreenSparklesMed,
    TngEffElm_RedSparklesMed,
    TngEffElm_BlueSparklesMed, // 70
    TngEffElm_WhiteSparklesLarge,
    TngEffElm_GreenSparklesLarge,
    TngEffElm_RedSparklesLarge,
    TngEffElm_BlueSparklesLarge,
    TngEffElm_RedSmokePuff,
    TngEffElm_BlueSmokePuff,
    TngEffElm_GreenSmokePuff,
    TngEffElm_YellowSmokePuff,
    TngEffElm_BluePuff,
    TngEffElm_GreenPuff, // 80
    TngEffElm_YellowPuff,
    TngEffElm_WhitePuff,
    TngEffElm_RedTwinkle3,
    TngEffElm_Thingy4,
    TngEffElm_BloodSplat,
    TngEffElm_BlueTwinkle,
    TngEffElm_GreenTwinkle,
    TngEffElm_YellowTwinkle,
    TngEffElm_CloudDisperse,
    TngEffElm_BlueTwinke2, // 90
    TngEffElm_GreenTwinkle2,
    TngEffElm_YellowTwinkle2,
    TngEffElm_RedDot,
    TngEffElm_IceMelt3,
    TngEffElm_DiseaseFly,
    TngEffElm_WhiteTwinkle,
    TngEffElm_WhiteTwinkle2,
    TngEffElm_WhiteFlame,
    TngEffElm_WhiteSmokePuff,
    TngEffElm_PurpleFlame, // 100
    TngEffElm_PurpleSmokePuff,
    TngEffElm_PurpleTwinkle,
    TngEffElm_PurpleTwinkle2,
    TngEffElm_PurplePuff,
    TngEffElm_BlackFlame,
    TngEffElm_BlackSmokePuff,
    TngEffElm_BlackTwinkle,
    TngEffElm_BlackTwinkle2,
    TngEffElm_BlackPuff,
    TngEffElm_OrangeFlame, // 110
    TngEffElm_OrangeSmokePuff,
    TngEffElm_OrangeTwinkle,
    TngEffElm_OrangeTwinkle2,
    TngEffElm_OrangePuff,
    TngEffElm_TinyFlash3,
    TngEffElm_StepSand,
    TngEffElm_StepGypsum,
    TngEffElm_GoldCoin
};

// Moved here from kfx_sim's creature_states.h (stage 13,
// docs/refactor/stage-13-enforce-and-document.md) -- config_creature.c/
// config_crtrmodel.c need the CrSt_* state-ID vocabulary to populate
// NamedCommand config-parsing tables and pass state IDs through
// set_creature_available()-style functions, without depending on
// creature_states.h's state-machine functions (kfx_sim, above
// kfx_config). Same "small vocabulary enum" treatment as ThingEffectKind
// above. config_crtrstates.c's own creature_states.h dependency runs
// deeper (its NamedField descriptor table also needs
// process_func_commands/cleanup_func_commands/etc, NamedCommand tables
// tied to creature_states.c's internal function-pointer arrays) and is
// left as an accepted residual.
enum CreatureStates {
    CrSt_Unused = 0,
    CrSt_ImpDoingNothing,
    CrSt_ImpArrivesAtDigDirt,
    CrSt_ImpArrivesAtMineGold,
    CrSt_ImpDigsDirt,
    CrSt_ImpMinesGold,
    CrSt_CreatureCastingPreparation,
    CrSt_ImpDropsGold,
    CrSt_ImpLastDidJob,
    CrSt_ImpArrivesAtImproveDungeon,
    CrSt_ImpImprovesDungeon,//[10]
    CrSt_CreaturePicksUpTrapObject,
    CrSt_CreatureArmsTrap,
    CrSt_CreaturePicksUpCrateForWorkshop,
    CrSt_MoveToPosition,
    CrSt_Null15,
    CrSt_CreatureDropsCrateInWorkshop,
    CrSt_CreatureDoingNothing,
    CrSt_CreatureToGarden,
    CrSt_CreatureArrivedAtGarden,
    CrSt_CreatureWantsAHome,//[20]
    CrSt_CreatureChooseRoomForLairSite,
    CrSt_CreatureAtNewLair,
    CrSt_PersonSulkHeadForLair,
    CrSt_PersonSulkAtLair,
    CrSt_CreatureGoingHomeToSleep,
    CrSt_CreatureSleep,
    CrSt_Null27,
    CrSt_Tunnelling,
    CrSt_Null29,
    CrSt_AtResearchRoom,//[30]
    CrSt_Researching,
    CrSt_AtTrainingRoom,
    CrSt_Training,
    CrSt_GoodDoingNothing,
    CrSt_GoodReturnsToStart,
    CrSt_GoodBackAtStart,
    CrSt_GoodDropsGold,
    CrSt_InPowerHand,
    CrSt_ArriveAtCallToArms,
    CrSt_CreatureArrivedAtPrison,//[40]
    CrSt_CreatureInPrison,
    CrSt_AtTortureRoom,
    CrSt_Torturing,
    CrSt_AtWorkshopRoom,
    CrSt_Manufacturing,
    CrSt_AtScavengerRoom,
    CrSt_Scavengering,
    CrSt_CreatureDormant, // For neutral creatures, moving around without purpose
    CrSt_CreatureInCombat,
    CrSt_CreatureLeavingDungeon,//[50]
    CrSt_CreatureLeaves,
    CrSt_CreatureInHoldAudience,
    CrSt_PatrolHere,
    CrSt_Patrolling,
    CrSt_Null55,
    CrSt_Null56,
    CrSt_Null57,
    CrSt_Null58,
    CrSt_CreatureKillCreatures,
    CrSt_CreatureKillDiggers,//[60]
    CrSt_PersonSulking,
    CrSt_Null62,
    CrSt_Null63,
    CrSt_AtBarrackRoom,
    CrSt_Barracking,
    CrSt_CreatureSlapCowers,
    CrSt_CreatureUnconscious,
    CrSt_CreaturePickUpUnconsciousBody,
    CrSt_ImpToking,
    CrSt_ImpPicksUpGoldPile,//[70]
    CrSt_MoveBackwardsToPosition,
    CrSt_CreatureDropBodyInPrison,
    CrSt_ImpArrivesAtConvertDungeon,
    CrSt_ImpConvertsDungeon,
    CrSt_CreatureWantsSalary,
    CrSt_CreatureTakeSalary,
    CrSt_TunnellerDoingNothing,
    CrSt_CreatureObjectCombat,
    CrSt_CreatureObjectSnipe,
    CrSt_CreatureChangeLair,//[80]
    CrSt_ImpBirth,
    CrSt_AtTemple,
    CrSt_PrayingInTemple,
    CrSt_CreatureOutOfPlay,
    CrSt_CreatureFollowLeader,
    CrSt_CreatureDoorCombat,
    CrSt_CreatureCombatFlee,
    CrSt_CreatureSacrifice,
    CrSt_AtLairToSleep,
    CrSt_CreatureExempt,//[90]
    CrSt_CreatureBeingDropped,
    CrSt_CreatureBeingSacrificed,
    CrSt_CreatureScavengedDisappear,
    CrSt_CreatureScavengedReappear,
    CrSt_CreatureBeingSummoned,
    CrSt_CreatureHeroEntering, // State used for hero entering from ceiling
    CrSt_ImpArrivesAtReinforce,
    CrSt_ImpReinforces,
    CrSt_ArriveAtAlarm,
    CrSt_CreaturePicksUpSpellObject,//[100]
    CrSt_CreatureDropsSpellObjectInLibrary,
    CrSt_CreaturePicksUpCorpse,
    CrSt_CreatureDropsCorpseInGraveyard,
    CrSt_AtGuardPostRoom,
    CrSt_Guarding,
    CrSt_CreatureEat,
    CrSt_CreatureEvacuateRoom,
    CrSt_CreatureWaitAtTreasureRoomDoor,
    CrSt_AtKinkyTortureRoom,
    CrSt_KinkyTorturing,//[110]
    CrSt_MadKillingPsycho,
    CrSt_CreatureSearchForGoldToStealInRoom1,
    CrSt_CreatureVandaliseRooms,
    CrSt_CreatureStealGold,
    CrSt_SeekTheEnemy,
    CrSt_AlreadyAtCallToArms,
    CrSt_CreatureDamageWalls,
    CrSt_CreatureAttemptToDamageWalls,
    CrSt_CreaturePersuade,
    CrSt_CreatureChangeToChicken,//[120]
    CrSt_CreatureChangeFromChicken,
    CrSt_ManualControl,
    CrSt_CreatureCannotFindAnythingToDo,
    CrSt_CreaturePiss,
    CrSt_CreatureRoar,
    CrSt_CreatureAtChangedLair,
    CrSt_CreatureBeHappy,
    CrSt_GoodLeaveThroughExitDoor,
    CrSt_GoodWaitInExitDoor,
    CrSt_GoodAttackRoom1,//[130]
    CrSt_CreatureSearchForGoldToStealInRoom2,
    CrSt_GoodArrivedAtSabotageRoom,
    CrSt_CreaturePretendChickenSetupMove,
    CrSt_CreaturePretendChickenMove,
    CrSt_CreatureAttackRooms,
    CrSt_CreatureFreezePrisoners,
    CrSt_CreatureExploreDungeon,
    CrSt_CreatureEatingAtGarden,
    CrSt_LeavesBecauseOwnerLost,
    CrSt_CreatureMoan,//[140]
    CrSt_CreatureSetWorkRoomBasedOnPosition,
    CrSt_CreatureBeingScavenged,
    CrSt_CreatureEscapingDeath,
    CrSt_CreaturePresentToDungeonHeart,
    CrSt_CreatureSearchForSpellToStealInRoom,
    CrSt_CreatureStealSpell,
    CrSt_GoodArrivedAtAttackRoom,
    CrSt_CreatureGoingToSafetyForToking,
    CrSt_Timebomb,
    CrSt_GoodWanderToCreatureCombat,//[150]
    CrSt_GoodWanderToObjectCombat,
    CrSt_CreatureDropBodyInLair,
    CrSt_CreatureSaveUnconsciousCreature,
    CrSt_ListEnd,
};

// Moved here from kfx_sim's player_instances.h (stage 13,
// docs/refactor/stage-13-enforce-and-document.md) -- config_rules.c/
// config_players.c and map_locations.c (kfx_sim itself) need this
// PLAYER*/ALL_PLAYERS vocabulary to populate/reference the player_desc
// NamedCommand table, without requiring the rest of player_instances.h's
// player-switching functions. Same "small vocabulary enum" treatment as
// CreatureStates above.
enum PlayerNames {
    PLAYER0          =  0,//red
    PLAYER1          =  1,//blue
    PLAYER2          =  2,//green
    PLAYER3          =  3,//yellow
    PLAYER_GOOD      =  4,//white
    PLAYER_NEUTRAL   =  5,
    PLAYER4          =  6,//purple
    PLAYER5          =  7,//black
    PLAYER6          =  8,//orange
    ALL_PLAYERS      =  9,
};

enum CreatureStateTypes {
    CrStTyp_Idle = 0,
    CrStTyp_Work,
    CrStTyp_OwnNeeds,
    CrStTyp_Sleep,
    CrStTyp_Feed,
    CrStTyp_FightCrtr,
    CrStTyp_Move,
    CrStTyp_GetsSalary,
    CrStTyp_Escape,
    CrStTyp_Unconscious,
    CrStTyp_AngerJob,
    CrStTyp_FightDoor,
    CrStTyp_FightObj,
    CrStTyp_Called2Arms,
    CrStTyp_Follow,
    CrStTyp_DeepWork,
    CrStTyp_ListEnd,
};

// Moved from kfx_config's config_keeperfx.h -- bflib_text.c (kfx_platform)
// uses these purely as a compile-time language-ID vocabulary for CJK
// font selection, same shape as the other enum moves above. See
// docs/refactor/stage-13-enforce-and-document.md.
enum TbLanguage {
    Lang_Unset    =  0,
    Lang_English,
    Lang_French,
    Lang_German,
    Lang_Italian,
    Lang_Spanish,
    Lang_Swedish,
    Lang_Polish,
    Lang_Dutch,
    Lang_Hungarian,
    Lang_Korean,
    Lang_Danish,
    Lang_Norwegian,
    Lang_Czech,
    Lang_Arabic,
    Lang_Russian,
    Lang_Japanese,
    Lang_ChineseInt,
    Lang_ChineseTra,
    Lang_Portuguese,
    Lang_Hindi,
    Lang_Bengali,
    Lang_Javanese,
    Lang_Latin,
    Lang_Ukrainian,
};

// Moved from kfx_sim's thing_traps.h -- config_trapdoor.c (kfx_config)
// uses these purely as a compile-time trap-trigger/activation-type
// vocabulary, same shape as the other enum moves above. See
// docs/refactor/stage-13-enforce-and-document.md.
enum TrapTriggerTypes {
    TrpTrg_None = 0,
    TrpTrg_LineOfSight90,
    TrpTrg_Pressure_Slab,
    TrpTrg_LineOfSight,
    TrpTrg_Pressure_Subtile,
    TrpTrg_Always,
};

enum TrapActivationTypes {
    TrpAcT_None = 0,
    TrpAcT_HeadforTarget90,
    TrpAcT_EffectonTrap,
    TrpAcT_ShotonTrap,
    TrpAcT_SlabChange,
    TrpAcT_CreatureShot,
    TrpAcT_CreatureSpawn,
    TrpAcT_Power,
};

// Moved from kfx_sim's creature_graphics.h -- config_crtrmodel.c
// (kfx_config) uses these purely as a compile-time graphics-sequence-ID
// vocabulary, same shape as the other enum moves above. See
// docs/refactor/stage-13-enforce-and-document.md.
enum CreatureGraphicsInstances {
    CGI_Stand        =  0,
    CGI_Ambulate     =  1,
    CGI_Drag         =  2,
    CGI_Attack       =  3,
    CGI_Dig          =  4,
    CGI_Smoke        =  5,
    CGI_Relax        =  6,
    CGI_PrettyDance  =  7,
    CGI_GotHit       =  8,
    CGI_PowerGrab    =  9,
    CGI_GotSlapped   = 10,
    CGI_Celebrate    = 11,
    CGI_Sleep        = 12,
    CGI_EatChicken   = 13,
    CGI_Torture      = 14,
    CGI_Scream       = 15,
    CGI_DropDead     = 16,
    CGI_DeadSplat    = 17,
    CGI_Roar         = 18,
    CGI_QuerySymbol  = 19, // Icon, not a sprite
    CGI_HandSymbol   = 20, // Icon, not a sprite
    CGI_Piss         = 21,
    CGI_CastSpell    = 22,
    CGI_RangedAttack = 23,
    CGI_Custom       = 24,
};

// Moved from kfx_sim's thing_list.h -- config_crtrmodel.c/value_util.c
// (kfx_config) use these purely as a compile-time class-ID vocabulary,
// same shape as the other enum moves above. See
// docs/refactor/stage-13-enforce-and-document.md.
enum ThingClassIndex {
    TCls_Empty        =  0,
    TCls_Object       =  1,
    TCls_Shot         =  2,
    TCls_EffectElem   =  3,
    TCls_DeadCreature =  4,
    TCls_Creature     =  5,
    TCls_Effect       =  6,
    TCls_EffectGen    =  7,
    TCls_Trap         =  8,
    TCls_Door         =  9,
    TCls_unusedparam10 = 10,
    TCls_unusedparam11 = 11,
    TCls_AmbientSnd   = 12,
    TCls_CaveIn       = 13,
};

// Moved from kfx_sim's thing_shots.h (stage 13.3) -- pure name/ID
// vocabulary for shot behavior, no functional coupling to struct Shot;
// config_magic.c needs these for its NamedCommand tables.
enum ShotFireLogics {
    ShFL_Default = 0,
    ShFL_Beam,
    ShFL_Breathe,
    ShFL_Hail,
    ShFL_Lizard,
    ShFL_Volley,
};

enum ShotUpdateLogics {
    ShUL_Default = 0,
    ShUL_Lightning,
    ShUL_Wind,
    ShUL_Grenade,
    ShUL_GodLightning,
    ShUL_Lizard,
    ShUL_GodLightBall,
    ShUL_TrapTNT,
    ShUL_TrapLightning,
};

// Moved from kfx_frontend's front_input.h (stage 13.3) -- pure name/ID
// vocabulary for keybinding actions; kfx_config's config_settings.h
// only needs GAME_KEYS_COUNT as an array bound (struct GameKey
// kbkeys[GAME_KEYS_COUNT]), not any individual Gkey_* value, but
// keeping the whole enum as a single source keeps that bound in sync
// automatically as keys are added/removed.
enum GameKeys {
    Gkey_MoveUp = 0,
    Gkey_MoveDown,
    Gkey_MoveLeft,
    Gkey_MoveRight,
    Gkey_RotateMod,
    Gkey_SpeedMod, // 5
    Gkey_RotateCW,
    Gkey_RotateCCW,
    Gkey_ZoomIn,
    Gkey_ZoomOut,
    Gkey_ZoomRoomTreasure, // 10
    Gkey_ZoomRoomLibrary,
    Gkey_ZoomRoomLair,
    Gkey_ZoomRoomPrison,
    Gkey_ZoomRoomTorture,
    Gkey_ZoomRoomTraining, // 15
    Gkey_ZoomRoomHeart,
    Gkey_ZoomRoomWorkshop,
    Gkey_ZoomRoomScavenger,
    Gkey_ZoomRoomTemple,
    Gkey_ZoomRoomGraveyard, // 20
    Gkey_ZoomRoomBarracks,
    Gkey_ZoomRoomHatchery,
    Gkey_ZoomRoomGuardPost,
    Gkey_ZoomRoomBridge,
    Gkey_ZoomRoomPortal, // 25
    Gkey_ZoomToFight,
    Gkey_ZoomCrAnnoyed,
    Gkey_CrtrContrlMod,
    Gkey_CrtrQueryMod,
    Gkey_DumpToOldPos, // 30
    Gkey_TogglePause,
    Gkey_SwitchToMap,
    Gkey_ToggleMessage,
    Gkey_SnapCamera,
    Gkey_BestRoomSpace, // 35
    Gkey_SquareRoomSpace,
    Gkey_RoomSpaceIncSize,
    Gkey_RoomSpaceDecSize,
    Gkey_SellTrapOnSubtile,
    Gkey_TiltUp, // 40
    Gkey_TiltDown,
    Gkey_TiltReset,
    Gkey_Ascend,
    Gkey_Descend,
    Gkey_ScreenRecord,
    Gkey_ScreenShot,
    Gkey_FrameSkipIncrease,
    Gkey_FrameSkipDecrease,
    Gkey_ZoomMinimapIn,
    Gkey_ZoomMinimapOut,
    Gkey_ToggleGui,
    Gkey_ToggleTooltips,
    Gkey_ExitGame,
    Gkey_DisablePacketMode,
    Gkey_ToggleConsole,
    Gkey_FinishLevel,
    Gkey_ToggleHeroHealthFlowers,
    Gkey_TeleportLastWorkroom,
    Gkey_TeleportCallToArms,
    Gkey_TeleportDefault,
    Gkey_CheatMenu1,
    Gkey_CheatMenu2,
    Gkey_LVShowAllEnsigns,
    Gkey_LVNextLevel,
    Gkey_LVPrevLevel,
    Gkey_NextInstance,
    Gkey_PrevInstance,
    Gkey_ButtonSnapLeft,
    Gkey_ButtonSnapRight,
    Gkey_ButtonSnapUp,
    Gkey_ButtonSnapDown,
    Gkey_PauseMenu,
    Gkey_LeftClick,
    Gkey_RightClick,
    Gkey_MouseUp,
    Gkey_MouseDown,
    Gkey_MouseLeft,
    Gkey_MouseRight,
    GAME_KEYS_COUNT
};

// docs/refactor/editor/10-definable-keybindings.md -- a separate key-binding
// namespace/storage for the in-game level editor, instead of folding editor
// shortcuts into GameKeys/game_key_settings[]/settings.kbkeys[] above. Most
// editor functions have no gameplay counterpart to collide with, so sharing
// one flat ~90-entry table just eats into that shared key space for no
// benefit -- separate storage means the editor can pick whatever keys make
// sense for it without the insertion-ordering/contiguity care
// GameKeys/BMV_Visible needed (see 10's own §2.2), and vice versa.
// Persisted, defaulted, and displayed the same way as GameKeys (a parallel
// settings.editor_kbkeys[EDITOR_GAME_KEYS_COUNT] + editor_key_settings[]
// table + its own Define-Keys-menu tab), just never mixed into the same
// array or enum.
enum EditorGameKeys {
    Gkey_EditorEraseTool = 0,
    // docs/refactor/editor/10-definable-keybindings.md -- camera/console
    // keys duplicated in from GameKeys, not shared with it: an editor
    // session's own camera navigation (get_isometric_view_nonaction_inputs(),
    // front_input.c) and console toggle read these instead of the gameplay
    // Gkey_MoveUp/etc. once an editor session is active, so the two can be
    // rebound independently -- the whole point of separate storage (10's
    // own intro) only pays off if editor code actually reads from it
    // instead of silently falling through to the gameplay table for
    // everything except the one tool shortcut added so far.
    Gkey_EditorMoveUp,
    Gkey_EditorMoveDown,
    Gkey_EditorMoveLeft,
    Gkey_EditorMoveRight,
    Gkey_EditorRotateMod,
    Gkey_EditorSpeedMod,
    Gkey_EditorRotateCW,
    Gkey_EditorRotateCCW,
    Gkey_EditorZoomIn,
    Gkey_EditorZoomOut,
    Gkey_EditorTiltUp,
    Gkey_EditorTiltDown,
    Gkey_EditorTiltReset,
    Gkey_EditorToggleConsole,
    // docs/refactor/editor/10-definable-keybindings.md -- tool-switch
    // hotkeys, first 3 of the toolbox's 12 tools (plus Gkey_EditorEraseTool
    // above): the ones toggled back to constantly *and* usable standalone,
    // with no follow-up model-picker click needed the way
    // Creature/Object/Trap/Door/Fill/Stamp all need -- picked as the
    // highest-value subset rather than giving all 12 tools a hotkey
    // (diminishing returns once a tool's own picker click dominates the
    // total action anyway). More can be added later if needed.
    Gkey_EditorTerrainTool,
    Gkey_EditorQueryTool,
    Gkey_EditorEyedropperTool,
    // fx-plans/00 item A11 -- the remaining tools that have no follow-up click
    // to make a hotkey pointless: Fill, Stamp, Points, Creature, Thing and
    // Reinforce (the last is a button tool, so the hotkey only opens it).
    Gkey_EditorFillTool,
    Gkey_EditorStampTool,
    Gkey_EditorPointsTool,
    Gkey_EditorCreatureTool,
    Gkey_EditorThingTool,
    Gkey_EditorReinforceTool,
    EDITOR_GAME_KEYS_COUNT
};

/**
 * Type to store menu number.
 */
typedef long MenuNumber;

// get_gameturn() itself (bflib_basics.c) is a thin wrapper over a
// registered provider -- kfx_game's game_legacy.c owns the real
// kfx_game_state read and is wired up as that provider from main.cpp.
// Every ERRORLOG/WARNLOG/etc. call site above is unaffected: they still
// just call get_gameturn(). See docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.
GameTurn get_gameturn(void);
typedef GameTurn (*GetGameTurnFunc)(void);
void set_get_gameturn_provider(GetGameTurnFunc provider);
#ifdef __cplusplus
}
#endif
#endif // KEEPFX_GLOBALS_H
