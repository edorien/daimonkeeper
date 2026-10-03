/**
 * @file external_seat.h
 * @brief Driver for External seats (docs/refactor/AI/LLM/): turns an agent's high-level verb
 * into the packets a human's input would produce, one packet per simulated turn, and gives the
 * agent control of the pause so it can think between turns.
 *
 * A verb becomes one to two steps; extseat_tick() writes the next step into the seat's
 * sim_packets[] slot once per simulated turn, right after exchange_packets() and before
 * process_packets() dispatches it -- the same seam ftest_packet_inject uses. Nothing is written
 * while the game is paused, so an action queued during a pause takes effect on the first turn
 * after it ends. The queue is process-global and not saved: an action in flight is dropped by a
 * load, which is the safe direction.
 */
#ifndef DK_EXTERNAL_SEAT_H
#define DK_EXTERNAL_SEAT_H

#include "globals.h"
#include "bflib_netsp.h"
#include "packet_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest gesture, in steps: a serpentine dig over the largest allowed area, or a sell of the largest area. */
#define EXTSEAT_MAX_STEPS 160
/** Area limits (slabs) for the area verbs, so a request cannot occupy the seat for minutes. */
#define EXTSEAT_MAX_DIG_ROWS 32
/** The engine keeps at most MAPTASKS_COUNT (300) dig marks per player, so more than this cannot all be tagged. */
#define EXTSEAT_MAX_DIG_SLABS 256
#define EXTSEAT_MAX_BUILD_SLABS 256
#define EXTSEAT_MAX_SELL_SLABS 64
#define EXTSEAT_MAX_OVERCHARGE_TURNS 32
/** Creatures sent somewhere with move_creature are tracked and handed back automatically, so an order the agent forgets never
 *  costs a creature its pay or its food (see extseat_auto_releases). */
#define EXTSEAT_MAX_ORDERED 32
#define EXTSEAT_AUTO_RELEASE_RING 8
/** Release when an enemy attacks it, pay day is this close (turns), the creature is owed pay, or its hunger reaches 3/4
 *  of its limit. */
#define EXTSEAT_ORDER_PAYDAY_MARGIN_TURNS 300
/** The shortest hold an order may ask for. The default hold is a quarter of the pay day gap, the longest half of it. */
#define EXTSEAT_ORDER_MIN_HOLD_TURNS 200
/** How long a hand step waits for the hand to be free, or for the picked creature to reach it, before giving up. */
#define EXTSEAT_HAND_WAIT_TURNS 30
/** send_message: at most this many characters (the engine's PLAYER_MP_MESSAGE_LEN, less its terminator), and at most
 *  one message per this many turns -- short messages, not a stream. */
#define EXTSEAT_CHAT_MAX_LEN 63
#define EXTSEAT_CHAT_MIN_TURNS 100
#define EXTSEAT_CHAT_LOG 16
/** How long a build_room waits for the seat's previous room drag to finish building before giving up (BUILD_BUSY). */
#define EXTSEAT_BUILD_WAIT_TURNS 600
/** Most things one pick_up_and_drop may carry (the hand's own limit, rules MaxThingsInHand, is checked too). */
#define EXTSEAT_MAX_HAND_SET 16
/** Verbs that may wait behind the running gesture (submitted with queue=true), and results remembered per seat. */
#define EXTSEAT_MAX_QUEUED_VERBS 16
#define EXTSEAT_RESULT_RING 16
/** Real-time limit for an agent-owned pause with no seat command before the game resumes by itself. */
#define EXTSEAT_DEFAULT_WATCHDOG_MS 60000

enum ExtSeatVerbKind {
    ESV_None = 0,
    ESV_PlaceTrap,
    ESV_PlaceDoor,
    ESV_Slap,
    ESV_PickUp,
    ESV_Drop,
    ESV_CastPower,
    ESV_BuildRoom,
    ESV_MarkDig,
    ESV_Sell,
    ESV_Cancel,
    ESV_MoveCreature,    /**< send one of our creatures to a subtile (the cheat menu's Order creature mode); it holds there until released */
    ESV_SetTendency,     /**< imprison / flee tendency of all the seat's creatures: name "imprison" or "flee", `enabled` */
    ESV_ReleaseCreature, /**< hand an ordered creature back to its normal behaviour */
    ESV_SetAlliance,     /**< declare (or withdraw) alliance with `target_player`: `enabled` */
    ESV_PickUpAndDrop,   /**< pick things up with the hand (our creatures, our prisoners, food) and drop them at one spot */
    ESV_UnmarkDig,       /**< take the dig marks off the marked slabs of a rectangle */
    ESV_SetDoorLock,     /**< lock (`enabled`) or unlock one of our doors, at `pos` */
    ESV_PowerOff,        /**< end a lasting power early: POWER_CALL_TO_ARMS, POWER_SIGHT, POWER_OBEY */
    ESV_UseSpecial,      /**< use a dungeon special box (thing_id); resurrect needs `name` + `level`, transfer `target_thing` */
    ESV_SendMessage,     /**< a short chat message to every player (`message`) */
};

/** A verb as the agent expressed it, already parsed out of JSON. Positions are subtile coordinates. */
struct ExtSeatVerb {
    enum ExtSeatVerbKind kind;
    char name[40];          /**< trap / door / power code name, when the verb has one */
    TbBool has_pos;
    int64_t stl_x, stl_y;
    TbBool has_thing;
    int64_t thing_id;
    TbBool has_rect;        /**< area verbs: an inclusive rectangle of SLABS, corners in any order */
    int64_t slab_x0, slab_y0, slab_x1, slab_y1;
    int64_t overcharge_turns; /**< cast_power on a subtile: turns to hold before releasing (level = turns / 4) */
    TbBool has_enabled;     /**< set_tendency: `enabled` was given */
    TbBool enabled;
    int64_t hold_turns;     /**< move_creature: automatic release after this many turns (0 = a quarter of the pay day gap) */
    int64_t expires_turn;   /**< 0 = never; else the verb is refused/dropped if it has not started by this game turn */
    TbBool has_target_player; /**< set_alliance: `target_player` was given */
    int64_t target_player;
    int64_t thing_ids[EXTSEAT_MAX_HAND_SET]; /**< pick_up_and_drop: several things in one trip (in place of thing_id) */
    int64_t thing_count;
    TbBool has_room;        /**< pick_up_and_drop / drop: `room_id` in place of `pos`, resolved to a spot inside it */
    int64_t room_id;
    TbBool has_direction;   /**< slap on a trap that shoots (a boulder): the way it goes, as a map angle */
    int64_t direction_angle;
    int64_t level;          /**< use_special (resurrect): the dead creature's level, 1-based */
    TbBool has_target_thing; /**< use_special (transfer): the creature to transfer */
    int64_t target_thing;
    char message[64];       /**< send_message: the text (PLAYER_MP_MESSAGE_LEN) */
    TbBool release;         /**< hand drops: dropping a prisoner outside your prison/torture chamber frees it; say so */
    TbBool sacrifice;       /**< hand drops: onto a temple pool (sacrificing the creature); refused otherwise */
};

/** What became of a submitted verb: it finished writing its steps, or was refused when its turn came. */
struct ExtSeatResult {
    int64_t id;
    TbBool rejected;
    char error[28];         /**< the error code when rejected ("EXPIRED", "NOT_AVAILABLE", "CANCELLED", ...) */
    int64_t turn;           /**< game turn it was decided */
};

/** A creature handed back to its own behaviour by the safety net rather than by the agent. */
struct ExtSeatAutoRelease {
    int64_t thing_id;
    char reason[16];        /**< "attacked", "max_hold", "payday", "owed_pay", "hungry", "agent_lost", "seat_released" */
    int64_t turn;
};

/** Outcome of a submit: how many steps it needs, its id, and where it stands. */
struct ExtSeatSubmitInfo {
    int64_t steps;
    int64_t id;
    int64_t queued_behind;  /**< verbs ahead of it (0 = starts now) */
};

struct ExtSeatStep {
    unsigned char action;
    int64_t par1, par2, par3, par4;
    TbBool has_pos;
    int64_t pos_x, pos_y;   /**< map coordinates (subtile centre), not subtiles */
    uint64_t control_flags;
    unsigned char context;  /**< cursor state carried with the position (CSt_PickAxe for digging) */
    ThingIndex follow_thing; /**< if set, the position is that creature's subtile at the moment the step is written */
    TbBool hand_set_drop;   /**< drops the top of the hand if it is one of the running verb's things (pick_up_and_drop);
                             *   when none of them is left in the hand the verb ends: PICK_UP_FAILED if none arrived,
                             *   PARTIAL if only some did -- whatever else might be held is never dropped */
    TbBool send_chat;       /**< the running verb's message goes into the player's pending chat message before this step */
    TbBool wait_build_free; /**< held back while the seat's previous room drag is still being built (one slab per turn):
                             *   the engine drops a new drag outright while one is in progress */
    TbBool wait_hand_free;  /**< held back while the hand is still grabbing or dropping (the engine ignores a hand action
                             *   then); gives up with HAND_BUSY after EXTSEAT_HAND_WAIT_TURNS */
};

/** Steps of a serpentine brush that tags every slab of the rectangle (see ftest_ai_gesture_drag_verbs for why a
 *  sweep and not the engine's own "drag" rectangle). Returns the step count, or 0 if it does not fit `max`. */
int64_t extseat_plan_dig_sweep(struct ExtSeatStep *out, int64_t max, int64_t x0, int64_t y0, int64_t x1, int64_t y1);
/** Steps that press and release once over each of `count` slabs (a click sells whatever is on the slab). */
int64_t extseat_plan_slab_clicks(struct ExtSeatStep *out, int64_t max, const int64_t *slab_xy, int64_t count);

/** Validates the verb for `plyr_idx` and queues its steps for `user`. Returns NULL on success, else a
 *  stable error code ("ACTION_ALREADY_QUEUED", "UNKNOWN_KIND", "NOT_AVAILABLE", ...). */
const char *extseat_submit_verb(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, int64_t *out_steps);
/** Like extseat_submit_verb, but with queue=true a verb submitted while a gesture is running waits its turn (up to
 *  EXTSEAT_MAX_QUEUED_VERBS, else "QUEUE_FULL") instead of being refused with ACTION_ALREADY_QUEUED. A waiting verb is
 *  validated now (cheap refusal) and again against the live state when it starts, so an order that went stale while
 *  it waited is dropped and reported through extseat_results(), never applied blindly. */
const char *extseat_submit_verb_ex(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, TbBool queue, struct ExtSeatSubmitInfo *info);
/** Validates `verb` for `plyr_idx` exactly as extseat_submit_verb_ex would, but never queues it, tracks it, or touches
 *  the seat's queue-busy state -- a dry_run: an agent can check "would this be accepted, and how many steps would it
 *  take" (e.g. before spending a real decision on an order it is not sure it can afford) at no cost, any number of
 *  times, whatever the seat's current queue looks like. NULL and *out_steps on success, else the same error code a
 *  real submit would give. */
const char *extseat_check_verb(NetUserId user, PlayerNumber plyr_idx, const struct ExtSeatVerb *verb, int64_t *out_steps);
/** Recent automatic releases, oldest first; returns how many were copied. */
int64_t extseat_auto_releases(NetUserId user, struct ExtSeatAutoRelease *out, int64_t max);
/** build_room planning with the engine's own per-slab rule (can_build_room_at_slab), in the order it builds: returns
 *  how many slabs of the rectangle would take the room; `corner_out` (may be NULL) is the drag start corner to use (0
 *  top-left, 1 bottom-right, 2 top-right, 3 bottom-left -- only matters for bridges, which need the player's land beside
 *  each slab by its turn); the slabs that would not are written to `bad_xy` (up to `max_bad` x,y pairs) and counted in
 *  `bad_count`. */
int64_t extseat_room_build_check(PlayerNumber plyr_idx, RoomKind rkind, int64_t x0, int64_t y0, int64_t x1, int64_t y1,
    int *corner_out, int64_t *bad_xy, int64_t max_bad, int64_t *bad_count);
/** A chat message as the game delivered it (any player's, the seat's own included). */
struct ExtSeatChat {
    int64_t player;
    char text[64];
    int64_t turn;
};
/** Called for every chat message the game delivers (process_gameplay_chat_message). */
void extseat_note_chat(PlayerNumber plyr_idx, const char *text);
/** How many chat messages have been delivered this game (for spotting new ones). */
int64_t extseat_chat_total(void);
/** Recent chat messages, oldest first; returns how many were copied. */
int64_t extseat_chat_log(struct ExtSeatChat *out, int64_t max);
/** Rectangles (x0, y0, x1, y1 slab quads) of mark_dig verbs submitted but not all tagged yet: the running one and the
 *  queued ones, in order. Returns how many were written (at most `max`). */
int64_t extseat_pending_dig_rects(NetUserId user, int64_t *out4, int64_t max);
/** Creatures currently tracked as ordered by this seat. */
int64_t extseat_ordered_count(NetUserId user);
/** Hands every creature this seat has ordered back to its normal behaviour now. */
void extseat_release_ordered_now(NetUserId user, const char *reason);
/** Verbs waiting behind the running gesture. */
int64_t extseat_queued_verbs(NetUserId user);
/** Recent outcomes, oldest first; returns how many were copied. */
int64_t extseat_results(NetUserId user, struct ExtSeatResult *out, int64_t max);
/** Queues raw steps for `user` (used by tests and by extseat_submit_verb). False if a gesture is still running. */
TbBool extseat_enqueue(NetUserId user, const struct ExtSeatStep *steps, int64_t count);
TbBool extseat_idle(NetUserId user);
/** Steps still to be written for `user`'s current gesture (0 = the seat can take a new verb). */
int64_t extseat_queued_steps(NetUserId user);
/** True between a step that pressed the button and the step that releases it. */
TbBool extseat_mid_gesture(NetUserId user);
/** Drops the steps still queued. If the button is down, queues the steps that safely end the gesture (leave the
 *  tool, release). Returns the number of steps dropped. */
int64_t extseat_cancel(NetUserId user);
/** Forgets `user`'s queued steps (the seat is being released or removed). */
void extseat_forget_user(NetUserId user);
/** Forgets every queued step and any agent-owned pause. Called when a game starts or is loaded. */
void extseat_reset(void);

/** Once per simulated turn, after exchange_packets(). */
void extseat_tick(void);
/** Every logic tick, from the API poll at the start of update(): ends an advance, runs the watchdog. */
void extseat_poll(void);

/** The agent pauses the game (and owns the pause until it resumes it or the watchdog does). */
void extseat_pause(void);
void extseat_resume(void);
/** Runs `turns` simulated turns, then pauses again. */
void extseat_advance(int64_t turns);
TbBool extseat_agent_owns_pause(void);
TbBool extseat_advancing(void);
/** Any seat command counts as the agent being alive. */
void extseat_note_activity(void);
/** The agent's connection went away: resume if the agent was holding the pause. */
void extseat_on_client_lost(void);
void extseat_set_watchdog_ms(int64_t ms);
/** Option B of 06 section 2.2, opt-in and off by default: when armed, the built-in AI takes over every External seat if
 *  the watchdog fires while the agent holds the pause, or if the agent's connection is lost. Cleared by extseat_reset(). */
void extseat_set_takeover(TbBool armed);
TbBool extseat_takeover_armed(void);

#ifdef __cplusplus
}
#endif

#endif
