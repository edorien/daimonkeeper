/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file thing_types.h
 *     struct Thing, every object in the world, and its flag enums.
 * @par Purpose:
 *     Part of kfx_model, the header-only layout library (refactor pass 2,
 *     S08, docs/refactor-pass2/stage-08-kfx-model-headers.md). Types, macros
 *     and static inline helpers only: no function prototypes, no extern data
 *     (check_layering.py --strict enforces it). The storage these types live
 *     in stays in kfx_sim, which includes this header from thing_data.h.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_KFX_MODEL_THING_TYPES_H
#define DK_KFX_MODEL_THING_TYPES_H

#include "globals.h"
#include "bflib_basics.h"

#include <stdlib.h> // llabs

/** Max amount of creatures supported on any map. */
#define CREATURES_COUNT       1024

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Enums for thing->alloc_flags bit fields. */
enum ThingAllocFlags {
    TAlF_Exists            = 0x01,
    TAlF_IsInMapWho        = 0x02,
    TAlF_IsInStrucList     = 0x04,
    TAlF_InDungeonList     = 0x08,
    TAlF_IsInLimbo         = 0x10,
    TAlF_IsControlled      = 0x20,
    TAlF_IsFollowingLeader = 0x40,
    TAlF_IsDragged         = 0x80,
};

/** Enum for specifying thing allocation pool type. */
enum ThingAllocationPool {
    ThingAllocation_Synced = 0,    /**< Allocate from synced thing pool */
    ThingAllocation_Unsynced = 1   /**< Allocate from unsynced thing pool */
};

/** Enums for thing->state_flags bit fields. */
enum ThingFlags1 {
    TF1_IsDragged1     = 0x01,
    TF1_InCtrldLimbo   = 0x02,
    TF1_PushAdd        = 0x04,
    TF1_PushOnce       = 0x08,
    // 0x10 was TF1_DoFootsteps (the local camera's; refactor pass 5 S04 moved it to kfx_game_local)
    TF1_Teleported     = 0x20,
    TF1_FallingIntoAbyss = 0x40,
};

enum ThingFlags2 {
    TF2_CreatureIsMoving              = 0x01,
    TF2_Spectator           = 0x02,
    TF2_SummonedCreature    = 0x04,
    TF2_CreatureOutOfPlay   = 0x08,
};

enum ThingRenderingFlags {
    TRF_Invisible      = 0x01, // Not Drawn
    TRF_Unshaded       = 0x02, // Not shaded

    TRF_Tint_1         = 0x04, // Tint1: light blend toward tint_colour (enemy creatures blinking to owner colour in combat)
    TRF_Tint_2         = 0x08, // Tint2: stronger blend toward tint_colour (the pale-blue freeze effect)
    TRF_Tint_Flags     = 0x0C, // Tint flags

    TRF_Transpar_8     = 0x10, // Used on chicken effect when creature is turned to chicken
    TRF_Transpar_4     = 0x20, // Used for Invisible creatures and traps -- more transparent
    TRF_Transpar_Alpha = 0x30,
    TRF_Transpar_Flags = 0x30,

    TRF_AnimateOnce    = 0x40,
};

 /**
  * Used for EffectElementConfigStats->size_change and Thing->size_change.
  *
  * See effect_element_stats[] for setting of size_change.
  */
enum ThingSizeChange {
  TSC_DontChangeSize         = 0x00, /**< Default behaviour. */
  TSC_ChangeSize             = 0x01, /**< Used when creature changing to/from chicken, and by TngEffElm_Cloud3. */
  TSC_ChangeSizeContinuously = 0x02, /**< Used by TngEffElm_IceShard. */
};


enum ThingMovementFlags {
    TMvF_Default            = 0x000, // Default.
    TMvF_IsOnWater          = 0x001, // The creature is walking on water.
    TMvF_IsOnLava           = 0x002, // The creature is walking on lava.
    TMvF_BeingSacrificed    = 0x004, // For creature falling in the temple pool, this informs its sacrificed state.
    TMvF_ZeroVerticalVelocity          = 0x008, // thing->veloc_base.z.val = 0;
    TMvF_GoThroughWalls     = 0x010,
    TMvF_Flying             = 0x020, // The creature is flying and can navigate in the air.
    TMvF_Immobile           = 0x040, // The creature cannot move.
    TMvF_IsOnSnow           = 0x080, // The creature leaves footprints on snow path.
    TMvF_MagicFall          = 0x100, // The creature does a free fall with magical effect, ie. it was just created with some initial velocity.
    TMvF_Grounded           = 0x200, // For creature which are normally flying, this informs that its grounded due to spells or its condition.
};

/******************************************************************************/
#pragma pack(1)

struct Room;

struct Thing {
    unsigned char alloc_flags;
    unsigned char state_flags;
    int64_t next_on_mapblk;
    int64_t prev_on_mapblk;
    unsigned char owner;
    unsigned char active_state;
    unsigned char continue_state;
    int64_t creation_turn;
    struct Coord3d mappos;
    union {
//TCls_Empty
//TCls_Object
      struct {
        int64_t gold_stored;
        int64_t unusedparam;
      } valuable;
      struct {
        int64_t life_remaining;
        char freshness_state;
        unsigned char possession_startup_timer;
        TbBool some_chicken_was_sacrificed;
        int64_t angle;
      } food;
      struct {
        unsigned char box_kind;
      } custom_box;
      struct {
        int64_t belongs_to;
        int64_t cssize;
        int64_t spr_size;
      } lair;
      struct {
        int64_t belongs_to;
        int64_t cssize;
        int64_t spr_size;
      } torturer;
      struct {
        unsigned char state;
      } call_to_arms_flag;
      struct {
        unsigned char countdown_UNUSED;
        unsigned char beat_direction;
      } heart;
      struct {
        unsigned char number;
      } hero_gate;
      struct {
        KeepPwrLevel power_level;
      } lightning;
      struct {
        int64_t belongs_to;
        unsigned char shspeed;
      } armor;
      struct {
        int64_t belongs_to;
        unsigned char effect_slot;
      } disease;
      struct {
        int64_t room_idx;
      } roomflag;
//TCls_Shot
      struct {
        unsigned char dexterity;
        int64_t damage;
        unsigned char hit_type;
        int64_t target_idx;
        CrtrExpLevel shot_level;
        struct Coord3d originpos;
        int64_t num_wind_affected;
        CctrlIndex wind_affected_creature[CREATURES_COUNT];  //list of wind affected Creatures
      } shot;
      struct {
        int64_t x;
        int64_t target_idx;
        unsigned char posint;
        unsigned char range;
      } shot_lizard;
//TCls_EffectElem
//TCls_DeadCreature
      struct {
          CrtrExpLevel exp_level;
          unsigned char laid_to_rest;
      } corpse;
//TCls_Creature
      struct {
        int64_t gold_carried;
        int64_t health_bar_turns;
        int64_t volley_repeat;
        TbBool volley_fire;
      } creature;
//TCls_Effect
      struct {
        int64_t parent_class_id;
        ThingModel parent_model;
        unsigned char hit_type;
      } shot_effect;
      struct {
        int64_t number;
      } price_effect;
//TCls_EffectGen
      struct {
      int64_t range;
      int64_t generation_delay;
      } effect_generator;
//TCls_Trap
      struct {
        unsigned char num_shots;
        unsigned char revealed;
        TbBool wait_for_rearm;
        TbBool volley_fire;
        GameTurn rearm_turn;
        GameTurn shooting_finished_turn;
        int64_t volley_repeat;
        int64_t volley_delay;
        int64_t firing_at;
        unsigned char flag_number;
      } trap;
//TCls_Door
      struct {
      int64_t orientation;
      unsigned char opening_counter;
      int64_t closing_counter;
      unsigned char is_locked;
      PlayerBitFlags revealed;
      } door;
//TCls_unusedparam10
//TCls_unusedparam11
//TCls_AmbientSnd
//TCls_CaveIn
      struct {
        unsigned char x;
        unsigned char y;
        int64_t time;
        ThingModel model;
      }cave_in;
    };
    ThingModel model;
    int64_t index;
    /** Parent index. The parent may either be a thing, or a slab index.
     * What it means depends on thing class, ie. it's thing index for shots
     *  and slab number for objects.
     */
    int64_t parent_idx;
    unsigned char class_id;
    unsigned char fall_acceleration;
    unsigned char bounce_angle;
    unsigned char abyss_fall_sound_delay;
    int64_t inertia_floor;
    int64_t inertia_air;
    int64_t movement_flags;
    struct CoordDelta3d veloc_push_once;
    struct CoordDelta3d veloc_base;
    struct CoordDelta3d veloc_push_add;
    struct CoordDelta3d velocity;
    // Push when moving; needs to be signed
    int64_t anim_speed;
    int64_t anim_time; // animation time (measured in 1/256 of a frame)
    int64_t anim_sprite;
    int64_t sprite_size;
    unsigned char current_frame;
    unsigned char max_frames;
    char transformation_speed;
    int64_t sprite_size_min;
    int64_t sprite_size_max;
    unsigned char rendering_flags;
    unsigned char draw_class; /**< See enum ObjectsDrawClasses for valid values. */
    unsigned char size_change; /**< See enum ThingSizeChange for valid values. */
    unsigned char tint_colour;
    int64_t move_angle_xy;
    int64_t move_angle_z;
    int64_t clipbox_size_xy;
    int64_t clipbox_size_z;
    int64_t solid_size_xy;
    int64_t solid_size_z;
    HitPoints health;
    int64_t floor_height;
    int64_t light_id;
    CctrlIndex ccontrol_idx;
    unsigned char snd_emitter_id;
    int64_t next_of_class;
    int64_t prev_of_class;
    uint64_t flags; //ThingAddFlags
    GameTurn last_turn_damaged;
    int64_t previous_floor_height;
    struct Coord3d previous_mappos;
    uint32_t random_seed; // 32-bit RNG state, see LbRandomSeries
    PlayerNumber holding_player;
};

enum ThingAddFlags //named this way because they were part of the ThingAdd structure
{
    TAF_ROTATED_SHIFT = 16,
    TAF_ROTATED_MASK = 0x070000,
};

#pragma pack()
/******************************************************************************/
/*
 * Whether moving from pos1 to pos2 crosses a subtile boundary on the X
 * (cross_x_) or Y (cross_y_) axis first. Pure geometry, moved here from
 * kfx_sim's thing_physics.c so Ariadne can use it directly (refactor pass 2,
 * S08).
 */
static inline TbBool cross_x_boundary_first(const struct Coord3d *pos1, const struct Coord3d *pos2)
{
    int64_t mul_x;
    int64_t mul_y;
    int64_t delta_x = pos2->x.val - (int64_t)pos1->x.val;
    int64_t delta_y = pos2->y.val - (int64_t)pos1->y.val;
    if (delta_x < 0)
    {
        mul_x = pos1->x.stl.pos;
  } else {
      mul_x = 255 - (int64_t)pos1->x.stl.pos;
  }
  if ( delta_y < 0 ) {
      mul_y = pos1->y.stl.pos;
  } else {
      mul_y = 255 - (int64_t)pos1->y.stl.pos;
  }
  return llabs(delta_x * mul_y) > llabs(mul_x * delta_y);
}

static inline TbBool cross_y_boundary_first(const struct Coord3d *pos1, const struct Coord3d *pos2)
{
    int64_t mul_x;
    int64_t mul_y;
    int64_t delta_x = pos2->x.val - (int64_t)pos1->x.val;
    int64_t delta_y = pos2->y.val - (int64_t)pos1->y.val;
    if (delta_x < 0)
    {
        mul_x = pos1->x.stl.pos;
  } else {
      mul_x = 255 - (int64_t)pos1->x.stl.pos;
  }
  if ( delta_y < 0 ) {
      mul_y = pos1->y.stl.pos;
  } else {
      mul_y = 255 - (int64_t)pos1->y.stl.pos;
  }
  return llabs(delta_y * mul_x) > llabs(mul_y * delta_x);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
