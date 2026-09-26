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
/** Release when pay day is this close (turns), or the creature is owed pay, or its hunger reaches 3/4 of its limit. */
#define EXTSEAT_ORDER_PAYDAY_MARGIN_TURNS 300
/** The shortest hold an order may ask for. The default hold is a quarter of the pay day gap, the longest half of it. */
#define EXTSEAT_ORDER_MIN_HOLD_TURNS 200
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
    ESV_ReleaseCreature, /**< hand an ordered creature back to its normal behaviour */
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
    int64_t hold_turns;     /**< move_creature: automatic release after this many turns (0 = a quarter of the pay day gap) */
    int64_t expires_turn;   /**< 0 = never; else the verb is refused/dropped if it has not started by this game turn */
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
    char reason[16];        /**< "max_hold", "payday", "owed_pay", "hungry", "agent_lost", "seat_released" */
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
/** Recent automatic releases, oldest first; returns how many were copied. */
int64_t extseat_auto_releases(NetUserId user, struct ExtSeatAutoRelease *out, int64_t max);
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
