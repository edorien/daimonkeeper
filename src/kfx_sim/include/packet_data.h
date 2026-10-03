/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file packet_data.h
 *     struct Packet and its trivial accessors, split out of kfx_net's
 *     packets.h (stage 13.3, docs/refactor/stage-13-enforce-and-document.md).
 * @par Purpose:
 *     struct Packet holds one player's resolved per-turn input (mouse
 *     position, action, control-key flags); kfx_sim (roomspace.c/
 *     roomspace_prediction.c/creature_instances.c) and kfx_render
 *     (cursor_tag.c/engine_redraw.c/local_camera.c) all dereference its
 *     fields directly and pervasively, not just via an occasional
 *     function call, so it must live at or below kfx_sim's own layer --
 *     same shape as camera_data.h's struct Camera split out of
 *     engine_camera.h. The accessor functions declared here
 *     (get_packet/get_packet_direct/set_packet_action/...) are trivial
 *     wrappers around sim_packets[] + get_player() -- both now defined
 *     here too (packet_data.c), moved down from kfx_net's
 *     packets.c/packets_misc.c/kfx_net_state.h (docs/refactor/todo/
 *     remove-symbol-level-layering-residuals.md) now that nothing about
 *     them is actually net-specific. kfx_net's own packet-exchange code
 *     still legitimately *writes* into sim_packets[] from above (a
 *     higher-ranked library writing into a lower-ranked library's state
 *     is fine -- only a lower library reaching upward is a violation).
 *     packets.h keeps re-including this header, so none of its other
 *     (same-or-higher-ranked) consumers need any changes.
 *
 *     The packet *processing* functions (process_packets/
 *     exchange_packets/...) stay declared in kfx_net's packets.h --
 *     they're genuine network/simulation orchestration, not data, and
 *     several of them reach into kfx_net_state and the ports directly.
 *     The camera handlers (process_camera_controls and friends) are in
 *     kfx_sim's player_camera.h since refactor pass 2's S07.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_PACKET_DATA_H
#define DK_PACKET_DATA_H

#include "bflib_basics.h"
#include "bflib_keybrd.h"
#include "bflib_netsp.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct PlayerInfo;

enum TbPacketAction {
        PckA_None = 0,
        PckA_QuitToMainMenu, // Quit
        PckA_ForceApplicationClose,
        PckA_UnusedSlot003,
        PckA_NoOperation,
        PckA_FinishGame, // 5
        PckA_Login,      // From `enum NetMessageType`
        PckA_UserUpdate,
        PckA_Frame,
        PckA_Resync,
        PckA_UnusedSlot010,//10
        PckA_UnusedSlot011,
        PckA_UnusedSlot012,
        PckA_PlyrMsgBegin,
        PckA_PlyrMsgEnd,
        PckA_UnusedSlot015,//15
        PckA_UnusedSlot016,
        PckA_UnusedSlot017,
        PckA_UnusedSlot018,
        PckA_UnusedSlot019,
        PckA_ToggleLights,//20
        PckA_UnusedSlot021,
        PckA_TogglePause,
        PckA_UnusedSlot023,
        PckA_SetCluedo,
        PckA_ChangeWindowSize,//25
        PckA_BookmarkLoad,
        PckA_SetGammaLevel,
        PckA_SetMinimapConf,
        PckA_SetMapRotation,
        PckA_UnusedSlot030,//30
        PckA_UnusedSlot031,
        PckA_PasngrCtrlExit,
        PckA_DirectCtrlExit,
        PckA_UnusedSlot034,
        PckA_UnusedSlot035,//35
        PckA_SetPlyrState,
        PckA_SwitchView,
        PckA_UnusedSlot038,
        PckA_CtrlCrtrSetInstnc,
        PckA_GenericLevelPower,//40
        PckA_HoldAudience,
        PckA_UnusedSlot042,
        PckA_UnusedSlot043,
        PckA_UnusedSlot044,
        PckA_UnusedSlot045,//45
        PckA_UnusedSlot046,
        PckA_UnusedSlot047,
        PckA_UnusedSlot048,
        PckA_UnusedSlot049,
        PckA_UnusedSlot050,//50
        PckA_UnusedSlot051,
        PckA_UnusedSlot052,
        PckA_UnusedSlot053,
        PckA_UnusedSlot054,
        PckA_ToggleTendency,//55
        PckA_UnusedSlot056,
        PckA_UnusedSlot057,
        PckA_UnusedSlot058,
        PckA_UnusedSlot059,
        PckA_CheatEnter,//60
        PckA_CheatAllFree,
        PckA_CheatCrtSpells, // unused
        PckA_CheatRevealMap,
        PckA_CheatCrAllSpls, // unused
        PckA_CheatUnusedPlaceholder065,//65
        PckA_CheatAllMagic,
        PckA_CheatAllRooms,
        PckA_CheatUnusedPlaceholder068,
        PckA_CheatUnusedPlaceholder069,
        PckA_CheatAllResrchbl,//70
        PckA_UnusedSlot071,
        PckA_UnusedSlot072,
        PckA_UnusedSlot073,
        PckA_UnusedSlot074,
        PckA_UnusedSlot075,//75
        PckA_UnusedSlot076,
        PckA_UnusedSlot077,
        PckA_UnusedSlot078,
        PckA_UnusedSlot079,
        PckA_SetViewType,//80
        PckA_ZoomFromMap,
        PckA_UpdatePause,
        PckA_ZoomToEvent,
        PckA_ZoomToRoom,
        PckA_ZoomToTrap,//85
        PckA_ZoomToDoor,
        PckA_ZoomToPosition,
        PckA_ToggleComputerProcessing,
        PckA_PwrCTADis,
        PckA_UsePwrHandPick,//90
        PckA_UsePwrHandDrop,
        PckA_EventBoxTurnOff,
        PckA_UseSpecialBox,
        PckA_UnusedSlot094,
        PckA_ResurrectCrtr,//95
        PckA_TransferCreatr,
        PckA_UsePwrObey,
        PckA_UsePwrArmageddon,
        PckA_TurnOffQuery,
        PckA_UnusedSlot100,//100
        PckA_UnusedSlot101,
        PckA_UnusedSlot102,
        PckA_UnusedSlot103,
        PckA_ZoomToBattle,
        PckA_UnusedSlot105,//105
        PckA_ZoomToSpell,
        PckA_ToggleComputer,
        PckA_PlyrFastMsg,
        PckA_SetComputerKind,
        PckA_GoSpectator,//110
        PckA_DumpHeldThingToOldPos,
        PckA_ToggleSpectate,
        PckA_UnusedSlot113,
        PckA_PwrSOEDis,
        PckA_EventBoxActivate,//115
        PckA_EventBoxClose,
        PckA_UsePwrOnThing,
        PckA_PlyrToggleAlly,
        PckA_SaveViewType,
        PckA_LoadViewType,//120
        PckA_UnusedSlot121    =  121,
        PckA_PlyrMsgClear,
        PckA_PlyrMsgLast,
        PckA_PlyrMsgCmdAutoCompletion,
        PckA_DirectCtrlDragDrop,
        PckA_CheatPlaceTerrain,
        PckA_CheatMakeCreature,
        PckA_CheatMakeDigger,
        PckA_CheatStealSlab,
        PckA_CheatStealRoom,
        PckA_CheatHeartHealth,
        PckA_CheatKillPlayer,
        PckA_CheatConvertCreature,
        PckA_CheatSwitchTerrain,
        PckA_CheatSwitchPlayer,
        PckA_CheatSwitchCreature,
        PckA_CheatSwitchHero,
        PckA_CheatSwitchExperience,
        PckA_CheatCtrlCrtrSetInstnc,
        PckA_SetFirstPersonDigMode,
        PckA_SwitchTeleportDest,
        PckA_SelectFPPickup,
        PckA_CheatAllDoors,
        PckA_CheatAllTraps,
        PckA_SetRoomspaceAuto,
        PckA_SetRoomspaceMan,
        PckA_SetRoomspaceDrag,
        PckA_SetRoomspaceDefault,
        PckA_SetRoomspaceWholeRoom,
        PckA_SetRoomspaceSubtile,
        PckA_SetRoomspaceHighlight,
        PckA_SetNearestTeleport,
        PckA_SetRoomspaceDragPaint,
        PckA_PlyrQueryCreature,
        PckA_CheatGiveDoorTrap,
        PckA_RoomspaceHighlightToggle,
        PckA_ApplyRoomspaceDigTag,
		PckA_CheatWinLevel,
		PckA_CheatLoseLevel,
		PckA_CheatLevelUp,
		PckA_CheatLevelDown,
		PckA_CheatApplySpell,
		PckA_CheatKillCreature,
        // docs/refactor/editor/02-editing-toolbox.md §2.3 -- appended, not
        // inserted -- same "these numeric values are saved" reasoning as
        // enum PlayerStates (config_players.h).
        PckA_EditorFloodFill,
        // §2.6 -- sent directly by kfx_editor (not generated by the normal
        // per-work-state click dispatch -- see PSt_EditorPlaceObject's own
        // comment), carrying the target subtile position (actn_par1/
        // actn_par2, x/y -- full int32 range, needed since actn_par3/4 are
        // only int16_t and a max-size map's subtile position overflows
        // that), then the object model (actn_par3) and owner (actn_par4).
        // Position is NOT read from the packet's own pos_x/pos_y -- those
        // are per-turn scratch state that input() populates and
        // exchange_packets() resets right after, so by the time this
        // render-phase click handler runs they're already back to (0,0) for
        // the next turn (found live: object placement silently no-op'd,
        // logged MapCoordsValid=0 pos=(0,0) on every attempt). kfx_editor
        // instead recomputes the world position itself via screen_to_map()
        // at click time and sends it explicitly.
        PckA_EditorPlaceObject,
        // §2.7 -- picker selection verbs, same shape as PckA_CheatSwitchTerrain/
        // PckA_CheatSwitchCreature above (unconditionally overwrite the
        // selection, no work-state check) but writing to UserState's own
        // pre-existing chosen_trap_kind/chosen_door_kind (used by the classic
        // workshop PSt_PlaceTrap/PSt_PlaceDoor already) rather than a
        // CheatSelection field. Appended here, not grouped with the other
        // CheatSwitch* entries above, for the same "never renumber" reason.
        PckA_CheatSwitchTrap,
        PckA_CheatSwitchDoor,
        // Placement itself: same free-placement shape as PckA_EditorFloodFill
        // (bypasses the workshop-stock/resource-cost check the classic
        // PSt_PlaceTrap/PSt_PlaceDoor dispatch has, since a blank editor map
        // never has any manufactured stock) -- carries trap/door model
        // (actn_par1) and owner (actn_par2); position IS safe to read from
        // the packet's own pos_x/pos_y here, unlike PckA_EditorPlaceObject,
        // because these are sent from packets_process_cheats()'s per-work-state
        // switch during input() itself, not from a later render-phase callback.
        PckA_EditorPlaceTrap,
        PckA_EditorPlaceDoor,
        // §2.4 -- Terrain "Rectangle" mode: PSt_EditorPlaceTerrainRect tracks
        // a drag (start subtile recorded client-side in packets_cheats.c on
        // PCtr_LBtnClick) and sends this once, on release, to apply the
        // chosen slab kind to the whole marked box in one action. All 6 of
        // the packet's scalar slots are used: pos_x/pos_y (already the
        // release point, courtesy of the normal input path) is corner 2;
        // actn_par1/actn_par2 (int32, full range like PckA_EditorPlaceObject)
        // is the recorded drag-start corner 1; actn_par3/actn_par4 (int16)
        // are slab kind and owner.
        PckA_EditorPlaceTerrainRect,
        // §2.7 (deferred item, now done) -- Ctrl+LMB on an existing door
        // toggles its lock via lock_door()/unlock_door() (thing_doors.c)
        // instead of placing a new one. Carries the door thing's own index
        // (actn_par1) rather than a position -- PSt_EditorPlaceDoor's
        // dispatch (packets_cheats.c) already resolved which door via
        // find_base_thing_on_mapwho() before sending this, so the handler
        // doesn't need to re-derive it from a slab position.
        PckA_EditorToggleDoorLock,
        // §4 -- Undo (v1: placements only -- see editor_journal.cpp's own
        // header comment for why terrain paint/fill/rectangle and Redo are
        // both deferred). kfx_editor sends this in response to Ctrl+Z,
        // carrying the thing index of the most recently journaled
        // placement (actn_par1) -- the journal itself lives client-side in
        // kfx_editor, populated via EditorPort's record_placement,
        // called from every editor placement handler below right after its
        // own create_*() call succeeds.
        PckA_EditorUndo,
        // §2.2 -- "Clear to Earth" area op. Same corner-carrying shape as
        // PckA_EditorPlaceTerrainRect (actn_par1/actn_par2 = drag-start
        // corner, pos_x/pos_y = release corner), minus the kind/owner
        // params -- always SlbT_EARTH/neutral, nothing to carry.
        PckA_EditorRectClearEarth,
        // §2.2 -- "Delete Things Inside". Same corner-carrying shape as
        // PckA_EditorRectClearEarth -- no per-thing params needed, the
        // handler sweeps and deletes whatever it finds in the box.
        PckA_EditorRectDeleteThings,
        // §2.2 -- "Set Owner". Same corner-carrying shape, plus the target
        // owner in actn_par3 (int16 -- plenty for a player number).
        PckA_EditorRectSetOwner,
        // §4 -- Redo counterparts for the four placement verbs that read
        // position from the packet's own *ambient* pos_x/pos_y rather than
        // a param (PckA_CheatMakeCreature/_MakeDigger/PckA_EditorPlaceTrap/
        // _PlaceDoor). Found live: writing the recorded position directly
        // onto the local packet before resending one of *those* verbs
        // didn't work -- get_dungeon_control_nonaction_inputs() (called
        // every real turn from input(), which runs again before the
        // render-phase-originated packet is actually processed) overwrites
        // pos_x/pos_y with whatever the mouse cursor is over *then*,
        // clobbering the recorded value the same way editor_journal.cpp's
        // render-phase write intended to survive but couldn't -- same root
        // cause class as the original PckA_EditorPlaceObject bug. Fix:
        // carry position explicitly instead, same shape as
        // PckA_EditorPlaceObject already does (actn_par1/actn_par2, full
        // int32 range) -- these four verbs exist purely so Redo has an
        // explicit-position counterpart for creation calls whose *normal*
        // (non-redo) verb can't be changed to take one without touching
        // the classic (non-editor) cheat menu's own use of
        // PckA_CheatMakeCreature/_MakeDigger.
        PckA_EditorRedoCreature,
        PckA_EditorRedoDigger,
        PckA_EditorRedoTrap,
        PckA_EditorRedoDoor,
        // docs/refactor/editor/09-toolbox-remainder.md §1 -- Object
        // placement's value-property slice, scoped to gold amount (the one
        // genuinely per-instance property the data model already has a
        // field for -- thing->valuable.gold_stored, read back by
        // gold_object_typical_value()/add_gold_to_pile(); "spellbook power"/
        // "special kind" from the original design have no per-instance
        // field to tweak here, so they're not attempted). Re-targets an
        // EXISTING gold-family object by index (actn_par1, same thing_idx-
        // keyed shape as PckA_EditorUndo/PckA_EditorToggleDoorLock) with a
        // new stored value (actn_par2, full int32 range -- a modded gold
        // rule could plausibly exceed actn_par3/4's int16_t) rather than
        // folding a 5th field into PckA_EditorPlaceObject itself -- that
        // packet's 4 param slots are already full (int32 x/y + int16 model/
        // owner). kfx_editor sends this instead of PckA_EditorPlaceObject
        // when a click lands on an existing gold object rather than empty
        // ground (a square can only hold one object, so placing a *new* one
        // there would fail anyway).
        PckA_EditorSetGoldValue,
        // docs/refactor/editor/09-toolbox-remainder.md §1 -- position-edit
        // follow-up (live-tester feedback: needed fine height control to
        // move a wall torch up its wall, not just re-place it at subtile
        // granularity). Generic thing_idx + x/y/z rather than an
        // object-specific verb, since Lights and Action Points (still
        // not-started toolbox entries, doc §2) will need the same X/Y/Z
        // editing once those tools exist. x/y need the full int32 range
        // position already established elsewhere in this enum (actn_par1/
        // actn_par2); thing_idx (THINGS_COUNT is 12288, comfortably under
        // int16_t's range) and z (map height is bounded far below a map's
        // horizontal extent) both fit actn_par3/actn_par4.
        PckA_EditorSetThingPosition,
        // docs/refactor/editor/04-views-camera-overlays.md -- View > 1st
        // Person. Same "explicit position, full int32 range" shape as
        // PckA_EditorPlaceObject (actn_par1/actn_par2 = x/y): the plain
        // PckA_GoSpectator relies on level_lost_go_first_person() finding
        // an existing owned creature to derive a spawn position from,
        // which an editor session's map often doesn't have (a fresh New
        // Map has none at all) -- this carries the position explicitly
        // instead (the current camera's own center, from kfx_editor).
        PckA_EditorGoSpectator,
};

/** Packet flags for non-action player operation. */
enum TbPacketControl {
        PCtr_None           = 0x0000,
        PCtr_ViewRotateCW   = 0x0001,
        PCtr_ViewRotateCCW  = 0x0002,
        PCtr_MoveUp         = 0x0004,
        PCtr_MoveDown       = 0x0008,
        PCtr_MoveLeft       = 0x0010,
        PCtr_MoveRight      = 0x0020,
        PCtr_ViewZoomIn     = 0x0040,
        PCtr_ViewZoomOut    = 0x0080,
        PCtr_LBtnClick      = 0x0100,
        PCtr_RBtnClick      = 0x0200,
        PCtr_LBtnHeld       = 0x0400,
        PCtr_RBtnHeld       = 0x0800,
        PCtr_LBtnRelease    = 0x1000,
        PCtr_RBtnRelease    = 0x2000,
        PCtr_Gui            = 0x4000,
        PCtr_MapCoordsValid = 0x8000,
        PCtr_ViewTiltUp     = 0x10000,
        PCtr_ViewTiltDown   = 0x20000,
        PCtr_ViewTiltReset  = 0x40000,
        PCtr_Ascend         = 0x80000,
        PCtr_Descend        = 0x100000,
        PCtr_ViewZoomPos    = 0x200000,
        PCtr_ViewRotatePos  = 0x400000,
        /* Keys held by the player when the packet was made, for the cheat,
           editor and query handlers (refactor pass 2, S12): packet
           application reads them here, never from the local keyboard, which
           belongs to whoever runs the game, not to the packet's player. */
        PCtr_ModRAlt        = 0x800000,
        PCtr_ModRShift      = 0x1000000,
        PCtr_ModCtrl        = 0x2000000, //!< Either Ctrl key.
        PCtr_ModLAlt        = 0x4000000,
        PCtr_ToggleDetails  = 0x8000000, //!< Right Shift tapped in the query-all cheat: toggle the terrain details line.
        /* Bits 28-30: the heart health cheat's step (enum PacketHeartHealthStep). */
};

#define PCtr_HeartHealthShift 28
#define PCtr_HeartHealthMask  (0x7u << PCtr_HeartHealthShift)

/** Heart health cheat keys, as packet bits (PCtr_HeartHealthMask). */
enum PacketHeartHealthStep {
    PHHS_None = 0,
    PHHS_Up1,
    PHHS_Down1,
    PHHS_Up100,
    PHHS_Down100,
};

/**
 * Additional packet flags
 */
enum TbPacketAddValues {
    PCAdV_None              = 0x00, //!< Dummy flag
    PCAdV_SpeedupPressed    = 0x01, //!< The keyboard modified used for speeding up camera movement is pressed.
    PCAdV_ContextMask       = 0x1E, //!< Instead of a single bit, this value stores is 4-bit integer; stores context of map coordinates. The context is used to set the Cursor State.
    PCAdV_CrtrContrlPressed = 0x20, //!< The keyboard modified used for creature control is pressed.
    PCAdV_CrtrQueryPressed  = 0x40, //!< The keyboard modified used for querying creatures is pressed.
    PCAdV_RotatePressed     = 0x80,
};

#define PCtr_LBtnAnyAction (PCtr_LBtnClick | PCtr_LBtnHeld | PCtr_LBtnRelease)
#define PCtr_RBtnAnyAction (PCtr_RBtnClick | PCtr_RBtnHeld | PCtr_RBtnRelease)
#define PCtr_HeldAnyButton (PCtr_LBtnHeld | PCtr_RBtnHeld)

#define INVALID_PACKET (&bad_packet)

/******************************************************************************/
#pragma pack(1)


/**
 * Stores data exchanged between players each turn and used to re-create their input.
 * Version number is only used for replay files; increment it if the packet layout changes.
 */
#define PACKET_VER 0
struct Packet {
    GameTurn turn;
    TbBigChecksum checksum; //! Checksum of the entire game state of the previous turn, used solely for desync detection
    int8_t input_lag_turns;
    uint8_t action; //! Action kind performed by the player which owns this packet
    int32_t actn_par1; //! Players action parameter #1
    int32_t actn_par2; //! Players action parameter #2
    int32_t pos_x; //! Mouse Cursor Position X
    int32_t pos_y; //! Mouse Cursor Position Y
    uint32_t control_flags;
    uint8_t additional_packet_values; // uses the flags and values from TbPacketAddValues

    // union on packet_action_has_camera_position()
    union
    {
        uint16_t cam_x;
        int16_t actn_par3; //! Players action parameter #3
    };

    // union on packet_action_has_camera_position()
    union
    {
        uint16_t cam_y;
        int16_t actn_par4; //! Players action parameter #4
    };
};

// save file header for .pck files.
// (Bump the version if this struct or the .pck format changes.)
#define PACKET_SAVE_HEAD_VER 2

enum PacketSaveHeadFlags {
    PSHF_Checksum   = 0x01,
    PSHF_Compressed = 0x02,
};

struct PacketSaveHead {
    int64_t game_ver_major;
    int64_t game_ver_minor;
    int64_t game_ver_release;
    int64_t game_ver_build;
    uint64_t level_num;
    PlayerBitFlags players_exist;
    PlayerBitFlags players_comp;
    uint64_t isometric_view_zoom_level;
    uint64_t frontview_zoom_level;
    int64_t isometric_tilt;
    unsigned char video_rotate_mode;
    uint8_t flags; // PacketSaveHeadFlags
    uint64_t action_seed;
    TbBool default_imprison_tendency;
    TbBool default_flee_tendency;
    TbBool skip_heart_zoom;
    TbBool highlight_mode;
    signed char user_players[MAX_NET_USERS];
    signed char recording_user;
    char frontend_alliances;
    char user_names[MAX_NET_USERS][20];
};

#pragma pack()

// Moved down from kfx_net's net_game.h (docs/refactor/todo/
// remove-symbol-level-layering-residuals.md) alongside sim_packets[]
// below -- both were the last thing keeping get_packet()/get_packet_direct()/
// set_packet_action()/set_players_packet_action()'s real implementations in
// kfx_net despite the interface already living here. kfx_net's own
// packets.c/packets_misc.c/net_exchange_gameplay.c still need this too, for
// their own genuinely net-owned logic (checksums, wire buffer packing,
// turn-history exchange) -- reached via packets.h's existing #include of
// this header, no new #include needed there.
#define PACKETS_COUNT 9

extern struct Packet bad_packet;

// Per-turn input packets, one per connected player slot. Moved down from
// kfx_net_state (kfx_net_state.h) for the same reason as PACKETS_COUNT
// above -- kfx_net's packet-exchange code still legitimately *writes*
// into this from above (a higher-ranked library writing into a
// lower-ranked library's storage is fine, only the reverse is a
// violation -- see this header's own file comment). Deliberately a bare
// extern, not folded into kfx_sim_state: it isn't one of architecture.md
// §6.2's three raw-blob sync payloads today, and folding it into
// kfx_sim_state would silently add it to all of them.
extern struct Packet sim_packets[PACKETS_COUNT];

/******************************************************************************/
struct Packet *get_local_packet(void);
TbBool packet_action_has_camera_position(enum TbPacketAction action);
TbBool packet_action_has_camera_angle(const struct Packet *pckt);
void packet_set_camera_position(struct Packet *pckt, MapCoord x, MapCoord y);
void packet_clear_camera_position(struct Packet *pckt);
TbBool packet_get_camera_position(const struct Packet *pckt, MapCoord *x, MapCoord *y);
NetUserId get_local_user(void);
struct Packet *get_packet(NetUserId user);
void set_packet_action(struct Packet *pckt, unsigned char pcktype, int64_t par1, int64_t par2, int64_t par3, int64_t par4);
TbBool is_packet_empty(const struct Packet *pckt);
void set_players_packet_action(struct PlayerInfo *player, unsigned char pcktype, uint64_t par1, uint64_t par2, int64_t par3, int64_t par4);
void set_packet_control(struct Packet *pckt, uint64_t flag);
void set_players_packet_control(struct PlayerInfo *player, uint64_t flag);
unsigned char get_players_packet_action(struct PlayerInfo *player);
void unset_packet_control(struct Packet *pckt, uint64_t flag);
void unset_players_packet_control(struct PlayerInfo *player, uint64_t flag);
void set_players_packet_position(struct Packet *pckt, int64_t x, int64_t y, unsigned char context);
TbBool packet_crtr_control_pressed(struct Packet *packet);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
