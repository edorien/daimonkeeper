// The observation half of an External seat (docs/refactor/AI/LLM/03-observation-api.md).
// "own" is the seat's own dungeon and is never filtered. Everything else (the map, and other players'
// creatures, rooms, traps and doors) passes the same visibility rules the human UI applies, using the
// same engine queries, so the agent cannot see what a human in that seat could not.
#include "pre_inc.h"
#include <json.h>
#include <json-dom.h>

#include <stdlib.h>
#include <string.h>

#include "api_seat_view.h"
#include "api_seat_decision.h"
#include "config_creature.h"
#include "config_crtrstates.h"
#include "config_magic.h"
#include "config_rules.h"
#include "config_terrain.h"
#include "config.h"
#include "config_campaigns.h"
#include "config_strings.h"
#include "config_trapdoor.h"
#include "creature_control.h"
#include "creature_states.h"
#include "creature_states_mood.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "player_data.h"
#include "room_data.h"
#include "room_library.h"
#include "creature_states.h"
#include "map_data.h"
#include "magic_powers.h"
#include "map_events.h"
#include "slab_data.h"
#include "tasks_list.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "power_specials.h"
#include "power_process.h"
#include "map_columns.h"
#include "power_hand.h"
#include "thing_objects.h"
#include "creature_states_tortr.h"
#include "creature_states_prisn.h"
#include "creature_states_gardn.h"
#include "thing_traps.h"
#include "player_availability.h"
#include "config_compp.h"
#include "player_computer_types.h"
#include "post_inc.h"

#define VIEW_LIST_CAP 2000

static const char *victory_name(unsigned char state)
{
    switch (state) {
    case VicS_WonLevel: return "won";
    case VicS_LostLevel: return "lost";
    default: return "undecided";
    }
}

static void add_pos(VALUE *parent, const char *key, int64_t x, int64_t y)
{
    VALUE *arr = value_dict_add(parent, key);
    value_init_array(arr);
    value_init_int32(value_array_append(arr), (int32_t)x);
    value_init_int32(value_array_append(arr), (int32_t)y);
}

// Names for AnnoyMotive (creature_control.h's AngR_*): a small, fixed, non-config vocabulary, so a lookup table here is
// simpler than a config-driven code_name function for a single caller.
static const char *annoy_motive_name(AnnoyMotive reason)
{
    switch (reason) {
    case AngR_NotPaid: return "not_paid";
    case AngR_Hungry: return "hungry";
    case AngR_NoLair: return "no_lair";
    case AngR_Other: return "other";
    default: return "none";
    }
}

static void add_creature_list(VALUE *arr, int64_t first, TbBool digger)
{
    int64_t idx = first;
    for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
        const struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing) || !thing_is_creature(thing)) {
            break;
        }
        const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
        VALUE *e = value_array_append(arr);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
        // The turn this creature came into being: with the id it tells a creature from a later one that reuses the
        // same thing slot (an identity, not information a human lacks).
        value_init_int64(value_dict_add(e, "born"), (int64_t)thing->creation_turn);
        value_init_string(value_dict_add(e, "kind"), creature_code_name(thing->model));
        value_init_int32(value_dict_add(e, "level"), (int32_t)(cctrl->exp_level + 1));
        value_init_int32(value_dict_add(e, "health"), (int32_t)thing->health);
        value_init_bool(value_dict_add(e, "digger"), digger);
        add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
        value_init_string(value_dict_add(e, "state"), creature_state_code_name(get_creature_state_besides_interruptions(thing)));
        // Why a creature is unhappy (already-tracked engine state, not a new signal: anger_is_creature_angry/
        // anger_get_creature_anger_type read cctrl->annoyance_level[] against the model's own annoy_level threshold) and
        // whether it is running from a fight, both otherwise invisible behind the raw "state" code above.
        const TbBool angry = anger_is_creature_angry(thing);
        value_init_bool(value_dict_add(e, "angry"), angry);
        if (angry) {
            value_init_string(value_dict_add(e, "angry_reason"), annoy_motive_name(anger_get_creature_anger_type(thing)));
        }
        value_init_bool(value_dict_add(e, "fleeing"), creature_is_fleeing_combat(thing));
        idx = cctrl->players_next_creature_idx;
    }
}

// One character per slab kind: printable, and never '.', which means "not revealed".
static char kind_char(SlabKind kind)
{
    static const char alphabet[] = "!\"#$%&'()*+,-/0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    return ((size_t)kind < sizeof(alphabet) - 1) ? alphabet[kind] : (char)0x23;
}

static TbBool slab_revealed(MapSlabCoord slb_x, MapSlabCoord slb_y, PlayerNumber plyr_idx)
{
    return subtile_revealed(slab_subtile_center(slb_x), slab_subtile_center(slb_y), plyr_idx);
}

// Mirrors tag_blocks_for_digging_in_area's test: the slab would take a dig mark from this seat.
static TbBool slab_takes_dig_mark(PlayerNumber plyr_idx, MapSlabCoord x, MapSlabCoord y)
{
    const struct Map *mapblk = get_map_block_at(slab_subtile_center(x), slab_subtile_center(y));
    struct SlabMap *slb = get_slabmap_block(x, y);
    const struct SlabConfigStats *slabst = get_slab_stats(slb);
    const TbBool revealed = map_block_revealed(mapblk, plyr_idx);
    return (slabst->is_diggable || !revealed)
        && (((mapblk->flags & SlbAtFlg_IsRoom) == 0) || (slabmap_owner(slb) != plyr_idx))
        && (((mapblk->flags & SlbAtFlg_Filled) == 0) || (slabmap_owner(slb) == plyr_idx) || !revealed);
}

// Ground an imp can stand on: open, seen, not lava, and not somebody else's door.
static TbBool slab_walkable_for(PlayerNumber plyr_idx, MapSlabCoord x, MapSlabCoord y)
{
    const struct Map *mapblk = get_map_block_at(slab_subtile_center(x), slab_subtile_center(y));
    const struct SlabMap *slb = get_slabmap_block(x, y);
    if ((mapblk->flags & SlbAtFlg_Blocking) != 0 || !map_block_revealed(mapblk, plyr_idx) || (slb->kind == SlbT_LAVA)) {
        return false;
    }
    return !slab_kind_is_door(slb->kind) || (slabmap_owner(slb) == plyr_idx);
}

enum { DigCell_Blocked = 0, DigCell_Walkable, DigCell_Marked, DigCell_Reached = 0x80 };

// Clamps a slab quad (any corner order) to the map as lx..rx, ty..by; false if nothing of it is on the map.
static TbBool clamp_slab_rect(const int64_t *q4, int64_t w, int64_t h, int64_t *lx, int64_t *ty, int64_t *rx, int64_t *by)
{
    *lx = (q4[0] < q4[2]) ? q4[0] : q4[2]; *rx = (q4[0] < q4[2]) ? q4[2] : q4[0];
    *ty = (q4[1] < q4[3]) ? q4[1] : q4[3]; *by = (q4[1] < q4[3]) ? q4[3] : q4[1];
    if (*lx < 0) *lx = 0;
    if (*ty < 0) *ty = 0;
    if (*rx >= w) *rx = w - 1;
    if (*by >= h) *by = h - 1;
    return (*lx <= *rx) && (*ty <= *by);
}

static void mark_rect_cells(unsigned char *cell, PlayerNumber plyr_idx, const int64_t *q4, int64_t w, int64_t h)
{
    int64_t lx, ty, rx, by;
    if (!clamp_slab_rect(q4, w, h, &lx, &ty, &rx, &by)) return;
    for (int64_t y = ty; y <= by; y++)
        for (int64_t x = lx; x <= rx; x++)
            if (slab_takes_dig_mark(plyr_idx, (MapSlabCoord)x, (MapSlabCoord)y)) cell[y * w + x] = DigCell_Marked;
}

int64_t api_seat_unreachable_dig_slabs(PlayerNumber plyr_idx, const int64_t *assume4, int64_t n_assume, const int64_t *report4,
    int64_t *out_xy, int64_t max)
{
    const struct Dungeon *dungeon = get_dungeon(plyr_idx);
    const MapSlabCoord w = kfx_sim_state.map_tiles_x, h = kfx_sim_state.map_tiles_y;
    if (dungeon_invalid(dungeon) || (w <= 0) || (h <= 0)) {
        return 0;
    }
    unsigned char *cell = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    int64_t *queue = (int64_t *)malloc(sizeof(int64_t) * (size_t)w * (size_t)h);
    if ((cell == NULL) || (queue == NULL)) {
        free(cell);
        free(queue);
        return 0;
    }
    int64_t qn = 0;
    for (MapSlabCoord y = 0; y < h; y++) {
        for (MapSlabCoord x = 0; x < w; x++) {
            if (slab_walkable_for(plyr_idx, x, y)) {
                cell[y * w + x] = DigCell_Walkable;
                if (slabmap_owner(get_slabmap_block(x, y)) == plyr_idx) {
                    cell[y * w + x] |= DigCell_Reached;
                    queue[qn++] = y * w + x;
                }
            }
        }
    }
    int64_t marks_used = dungeon->highest_task_number;
    if (marks_used > MAPTASKS_COUNT) marks_used = MAPTASKS_COUNT;
    for (int64_t i = 0; i < marks_used; i++) {
        const struct MapTask *task = &dungeon->task_list[i];
        if (task->kind == 0) continue;
        const MapSlabCoord x = subtile_slab(stl_num_decode_x(task->coords)), y = subtile_slab(stl_num_decode_y(task->coords));
        if ((x >= 0) && (x < w) && (y >= 0) && (y < h)) cell[y * w + x] = DigCell_Marked;
    }
    {
        int64_t pending[4 * EXTSEAT_MAX_QUEUED_VERBS + 4];
        const int64_t np = extseat_pending_dig_rects(get_player(plyr_idx)->user_id, pending, EXTSEAT_MAX_QUEUED_VERBS + 1);
        for (int64_t i = 0; i < np; i++) mark_rect_cells(cell, plyr_idx, &pending[4 * i], w, h);
    }
    for (int64_t i = 0; (assume4 != NULL) && (i < n_assume); i++) mark_rect_cells(cell, plyr_idx, &assume4[4 * i], w, h);
    int64_t lx = 0, rx = -1, ty = 0, by = -1;
    if (report4 != NULL) {
        if (!clamp_slab_rect(report4, w, h, &lx, &ty, &rx, &by)) { free(cell); free(queue); return 0; }
        mark_rect_cells(cell, plyr_idx, report4, w, h);
    }
    // Flood: an imp reaches a mark by standing on a neighbouring slab (the four sides, as check_place_to_dig does), and
    // the fill carries on through marks, which are floor once dug.
    static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    for (int64_t qh = 0; qh < qn; qh++) {
        const int64_t at = queue[qh];
        const int64_t x = at % w, y = at / w;
        for (int d = 0; d < 4; d++) {
            const int64_t nx = x + dx[d], ny = y + dy[d];
            if ((nx < 0) || (nx >= w) || (ny < 0) || (ny >= h)) continue;
            unsigned char *c = &cell[ny * w + nx];
            if (((*c & DigCell_Reached) != 0) || (*c == DigCell_Blocked)) continue;
            *c |= DigCell_Reached;
            queue[qn++] = ny * w + nx;
        }
    }
    free(queue);
    int64_t n = 0;
    for (int64_t y = 0; y < h; y++) {
        for (int64_t x = 0; x < w; x++) {
            if (cell[y * w + x] != DigCell_Marked) continue; // reached marks carry the flag, so this is only unreached ones
            if ((report4 != NULL) && ((x < lx) || (x > rx) || (y < ty) || (y > by))) continue;
            if ((out_xy != NULL) && (n < max)) { out_xy[2 * n] = x; out_xy[2 * n + 1] = y; }
            n++;
        }
    }
    free(cell);
    return n;
}

// The map at slab granularity (the engine's own unit for owner and kind): two characters per slab,
// slab kind then owner, ".." where the seat has not revealed the slab. A slab counts as revealed when
// its centre subtile is (allies' vision included when the rules share it), the same test used for
// creatures and rooms below.
static void add_map(VALUE *out, PlayerNumber plyr_idx)
{
    const MapSlabCoord w = kfx_sim_state.map_tiles_x, h = kfx_sim_state.map_tiles_y;
    VALUE *map = value_dict_add(out, "map");
    value_init_dict(map);
    value_init_int32(value_dict_add(map, "width"), (int32_t)w);
    value_init_int32(value_dict_add(map, "height"), (int32_t)h);
    value_init_string(value_dict_add(map, "cell"), "kind char + owner digit; '..' = not revealed");
    TbBool seen[128];
    memset(seen, 0, sizeof(seen));
    VALUE *rows = value_dict_add(map, "rows");
    value_init_array(rows);
    char *line = (char *)malloc((size_t)w * 2 + 1);
    if (line == NULL) {
        return;
    }
    for (MapSlabCoord y = 0; y < h; y++) {
        for (MapSlabCoord x = 0; x < w; x++) {
            char *c = &line[x * 2];
            if (!slab_revealed(x, y, plyr_idx)) {
                c[0] = c[1] = '.';
                continue;
            }
            const struct SlabMap *slb = get_slabmap_block(x, y);
            c[0] = kind_char(slb->kind);
            c[1] = (slb->owner >= 0 && slb->owner < 10) ? (char)('0' + slb->owner) : '#';
            if ((uint64_t)slb->kind < sizeof(seen)) seen[slb->kind] = true;
        }
        line[w * 2] = 0;
        value_init_string(value_array_append(rows), line);
    }
    free(line);
    VALUE *legend = value_dict_add(map, "legend");
    value_init_array(legend);
    for (int64_t k = 0; k < (int64_t)sizeof(seen); k++) {
        if (!seen[k]) continue;
        VALUE *e = value_array_append(legend);
        value_init_dict(e);
        char ch[2] = { kind_char((SlabKind)k), 0 };
        value_init_string(value_dict_add(e, "c"), ch);
        value_init_string(value_dict_add(e, "kind"), slab_code_name((SlabKind)k));
    }
}

// A creature the seat may be told about: on a revealed subtile, and not an invisible enemy. Invisible
// allies and the seat's own creatures stay visible, as in engine_render.c's enemy-and-visible test.
static TbBool creature_visible_to(const struct Thing *thing, PlayerNumber plyr_idx)
{
    if (thing->owner == plyr_idx) {
        return true;
    }
    if (!thing_revealed(thing, plyr_idx)) {
        return false;
    }
    return !(players_are_enemies(plyr_idx, thing->owner) && creature_is_invisible(thing));
}

// Hidden traps stay hidden from other players until they are revealed (the tooltip, parchment and renderer
// all use this test); a trap on an unrevealed tile is unseen either way.
static TbBool trap_visible_to(const struct Thing *thing, PlayerNumber plyr_idx)
{
    if (thing->owner == plyr_idx) {
        return true;
    }
    if (!thing_revealed(thing, plyr_idx)) {
        return false;
    }
    return (thing->trap.revealed != 0) || !get_trap_model_stats(thing->model)->hidden;
}

static TbBool door_visible_to(struct Thing *thing, PlayerNumber plyr_idx)
{
    if (thing->owner == plyr_idx) {
        return true;
    }
    return thing_revealed(thing, plyr_idx) && !door_is_hidden_to_player(thing, plyr_idx);
}

static void add_owner_fields(VALUE *e, const struct Thing *thing, PlayerNumber plyr_idx)
{
    value_init_int32(value_dict_add(e, "owner"), (int32_t)thing->owner);
    value_init_bool(value_dict_add(e, "ally"), (thing->owner != plyr_idx) && players_are_mutual_allies(plyr_idx, thing->owner));
}

static void add_traps_and_doors(VALUE *traps, VALUE *doors, PlayerNumber plyr_idx, TbBool own)
{
    int64_t idx = kfx_sim_state.thing_lists[TngList_Traps].index;
    for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
        struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        if (!thing_is_deployed_trap(thing) || ((thing->owner == plyr_idx) != own) || !trap_visible_to(thing, plyr_idx)) continue;
        VALUE *e = value_array_append(traps);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
        value_init_string(value_dict_add(e, "kind"), trap_code_name(thing->model));
        if (own) {
            // Whether a slap sets it off now (and, for one that shoots, that the slap needs a direction).
            value_init_bool(value_dict_add(e, "slappable"), thing_slappable(thing, plyr_idx));
            const struct TrapConfigStats *trapst = get_trap_model_stats(thing->model);
            if ((trapst->activation_type == TrpAcT_HeadforTarget90) || (trapst->activation_type == TrpAcT_CreatureShot))
                value_init_bool(value_dict_add(e, "aimed"), true);
        }
        if (!own) add_owner_fields(e, thing, plyr_idx);
        add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
    }
    idx = kfx_sim_state.thing_lists[TngList_Doors].index;
    for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
        struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        if (!thing_is_deployed_door(thing) || ((thing->owner == plyr_idx) != own) || !door_visible_to(thing, plyr_idx)) continue;
        VALUE *e = value_array_append(doors);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
        value_init_string(value_dict_add(e, "kind"), door_code_name(thing->model));
        value_init_bool(value_dict_add(e, "locked"), thing->door.is_locked != 0);
        if (!own) add_owner_fields(e, thing, plyr_idx);
        add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
    }
}

// A creature's worth in a fight: the engine's own per-kind, per-level score (the table the dungeon score uses), scaled
// by how much health it has left, so a half-dead army counts as half.
static int64_t creature_force_score(const struct Thing *thing)
{
    const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
    const int64_t full = get_creature_thing_score(thing);
    if (creature_control_invalid(cctrl) || (cctrl->max_health <= 0)) return full;
    const int64_t hp = (thing->health < 0) ? 0 : ((thing->health > cctrl->max_health) ? cctrl->max_health : thing->health);
    return full * hp / cctrl->max_health;
}

// Fighters and workers (diggers: imps, tunnellers -- they dig, claim and carry, and barely fight) by owner.
struct ForceTally { int64_t fighters, workers, score; };

static void add_force(VALUE *parent, const char *key, const struct ForceTally *t)
{
    VALUE *f = value_dict_add(parent, key);
    value_init_dict(f);
    value_init_int64(value_dict_add(f, "fighters"), t->fighters);
    value_init_int64(value_dict_add(f, "workers"), t->workers);
    value_init_int64(value_dict_add(f, "fighter_score"), t->score);
}

// How well a room works, as its query panel and the map show a human: efficiency (it scales what the room produces and
// holds), health (whether it is being claimed away), how full it is, who is in it, and which of its edges are open --
// the sides room_slab_side_score gives nothing (a corridor, another room), the ones walling in or a door would improve.
#define OPEN_SIDE_EXAMPLES 8
static void add_room_quality(VALUE *e, const struct Room *room)
{
    value_init_int32(value_dict_add(e, "efficiency"), (int32_t)(room->efficiency * 100 / ROOM_EFFICIENCY_MAX));
    value_init_int64(value_dict_add(e, "health"), (int64_t)room->health);
    value_init_int64(value_dict_add(e, "max_health"), (int64_t)compute_room_max_health(room->slabs_count, room->efficiency));
    if (room->total_capacity > 0) {
        VALUE *cap = value_dict_add(e, "capacity");
        value_init_dict(cap);
        value_init_int64(value_dict_add(cap, "used"), room->used_capacity);
        value_init_int64(value_dict_add(cap, "total"), room->total_capacity);
    }
    if (room_role_matches(room->kind, RoRoF_GoldStorage)) {
        value_init_int64(value_dict_add(e, "gold"), (int64_t)room->capacity_used_for_storage);
    }
    // Who is in it, by kind: a lair keeps per-kind counts; other rooms list their workers (or prisoners) directly.
    VALUE *occ = value_dict_add(e, "occupants");
    value_init_dict(occ);
    const int64_t nmodels = kfx_config_state.conf.crtr_conf.model_count;
    int64_t counts[CREATURE_TYPES_MAX];
    memset(counts, 0, sizeof(counts));
    if (room_role_matches(room->kind, RoRoF_LairStorage)) {
        for (int64_t m = 1; (m < CREATURE_TYPES_MAX) && (m <= nmodels); m++) counts[m] = room->content_per_model[m];
    } else {
        int64_t i = room->creatures_list;
        for (int64_t guard = 0; (i > 0) && (guard < VIEW_LIST_CAP); guard++) {
            const struct Thing *thing = thing_get((ThingIndex)i);
            if (thing_is_invalid(thing) || !thing_is_creature(thing)) break;
            if ((thing->model > 0) && (thing->model < CREATURE_TYPES_MAX)) counts[thing->model]++;
            i = creature_control_get_from_thing(thing)->next_in_room;
        }
    }
    for (int64_t m = 1; (m < CREATURE_TYPES_MAX) && (m <= nmodels); m++) {
        if (counts[m] > 0) value_init_int64(value_dict_add(occ, creature_code_name((ThingModel)m)), counts[m]);
    }
    // Open sides, with a few examples: [slab x, slab y, side].
    static const struct { int dx, dy; const char *name; } sides[4] = { { 0, -1, "N" }, { 1, 0, "E" }, { 0, 1, "S" }, { -1, 0, "W" } };
    const int64_t synergy = get_room_kind_stats(room->kind)->synergy_slab;
    int64_t open = 0;
    VALUE *os = value_dict_add(e, "open_sides");
    value_init_dict(os);
    VALUE *ex = value_dict_add(os, "slabs");
    value_init_array(ex);
    SlabCodedCoords sn = room->slabs_list;
    for (int64_t guard = 0; (sn != 0) && (guard < VIEW_LIST_CAP); guard++) {
        const MapSlabCoord x = slb_num_decode_x(sn), y = slb_num_decode_y(sn);
        for (int d = 0; d < 4; d++) {
            const MapSlabCoord nx = x + sides[d].dx, ny = y + sides[d].dy;
            if ((nx < 0) || (ny < 0) || (nx >= kfx_sim_state.map_tiles_x) || (ny >= kfx_sim_state.map_tiles_y)) continue;
            if (room_slab_side_score(sn, get_slab_number(nx, ny), synergy, NULL) != 0) continue;
            if (open < OPEN_SIDE_EXAMPLES) {
                VALUE *s3 = value_array_append(ex);
                value_init_array(s3);
                value_init_int32(value_array_append(s3), (int32_t)x);
                value_init_int32(value_array_append(s3), (int32_t)y);
                value_init_string(value_array_append(s3), sides[d].name);
            }
            open++;
        }
        sn = get_next_slab_number_in_room(sn);
    }
    value_init_int64(value_dict_add(os, "count"), open);
}

// Custody (docs/refactor/AI/omissions/05): the seat's prisons and torture chambers, the enemies held in them (health,
// hunger, what each would become for the seat if it died there, time on the rack), its own creatures an enemy holds,
// and the chickens in its hatcheries that the hand could carry to a prison. Only present when something applies.
static int64_t torture_devices_free(const struct Room *room)
{
    int64_t free_devices = 0;
    SlabCodedCoords sn = room->slabs_list;
    for (int64_t guard = 0; (sn != 0) && (guard < VIEW_LIST_CAP); guard++) {
        const struct Thing *dev = find_base_thing_on_mapwho(TCls_Object, ObjMdl_Torturer,
            slab_subtile_center(slb_num_decode_x(sn)), slab_subtile_center(slb_num_decode_y(sn)));
        if (!thing_is_invalid(dev) && (dev->torturer.belongs_to == 0)) free_devices++;
        sn = get_next_slab_number_in_room(sn);
    }
    return free_devices;
}

static void add_custody(VALUE *own, PlayerNumber plyr_idx, const struct Dungeon *dungeon)
{
    VALUE cust_real; VALUE *cust = &cust_real;
    value_init_dict(cust);
    TbBool any = false;
    VALUE *prisons = NULL, *tortures = NULL, *food = NULL;
    for (int64_t rkind = 1; rkind < TERRAIN_ITEMS_MAX; rkind++) {
        const TbBool prison = room_role_matches((RoomKind)rkind, RoRoF_Prison);
        const TbBool torture = room_role_matches((RoomKind)rkind, RoRoF_Torture);
        const TbBool hatchery = room_role_matches((RoomKind)rkind, RoRoF_FoodSpawn);
        if (!prison && !torture && !hatchery) continue;
        int64_t ridx = dungeon->room_list_start[rkind];
        for (int64_t guard = 0; (ridx > 0) && (guard < VIEW_LIST_CAP); guard++) {
            const struct Room *room = room_get((RoomIndex)ridx);
            if (room_is_invalid(room)) break;
            ridx = room->next_of_owner;
            if (hatchery) continue; // counted with the chickens below
            VALUE **arr = prison ? &prisons : &tortures;
            if (*arr == NULL) { *arr = value_dict_add(cust, prison ? "prisons" : "torture"); value_init_array(*arr); }
            VALUE *e = value_array_append(*arr);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "room"), (int32_t)room->index);
            value_init_int64(value_dict_add(e, "held"), room->used_capacity);
            value_init_int64(value_dict_add(e, "capacity"), room->total_capacity);
            if (torture) value_init_int64(value_dict_add(e, "devices_free"), torture_devices_free(room));
            add_pos(e, "pos", room->central_stl_x, room->central_stl_y);
            any = true;
        }
    }
    VALUE *prisoners = NULL, *held = NULL;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
        const struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        if (!thing_is_creature(thing)) continue;
        const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
        if (thing->owner == plyr_idx) {
            if (!creature_is_kept_in_custody_by_enemy(thing)) continue;
            if (held == NULL) { held = value_dict_add(cust, "held_by_enemy"); value_init_array(held); }
            VALUE *e = value_array_append(held);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
            value_init_string(value_dict_add(e, "kind"), creature_code_name(thing->model));
            value_init_int32(value_dict_add(e, "level"), (int32_t)(cctrl->exp_level + 1));
            add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
            any = true;
            continue;
        }
        if (!creature_is_kept_in_custody_by_player(thing, plyr_idx)) continue;
        if (prisoners == NULL) { prisoners = value_dict_add(cust, "prisoners"); value_init_array(prisoners); }
        VALUE *e = value_array_append(prisoners);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
        value_init_string(value_dict_add(e, "kind"), creature_code_name(thing->model));
        value_init_int32(value_dict_add(e, "level"), (int32_t)(cctrl->exp_level + 1));
        value_init_int32(value_dict_add(e, "owner"), (int32_t)thing->owner);
        value_init_int64(value_dict_add(e, "health"), (int64_t)thing->health);
        value_init_int64(value_dict_add(e, "max_health"), (int64_t)cctrl->max_health);
        value_init_bool(value_dict_add(e, "hungry"), hunger_is_creature_hungry(thing));
        const struct Room *room = get_room_thing_is_on(thing);
        const char *where = thing_is_picked_up_by_player(thing, plyr_idx) ? "hand"
            : creature_is_being_tortured(thing) ? "torture" : creature_is_kept_in_prison(thing) ? "prison" : "moving";
        value_init_string(value_dict_add(e, "where"), where);
        if (!room_is_invalid(room)) value_init_int32(value_dict_add(e, "room"), (int32_t)room->index);
        // What it would become for the seat if it died where it is (whether it does is the rules' chance).
        ThingModel becomes = 0;
        if (!room_is_invalid(room) && (strcmp(where, "prison") == 0) && (kfx_config_state.conf.rules[plyr_idx].rooms.prison_skeleton_chance > 0))
            becomes = prison_death_kind(thing, room);
        else if (!room_is_invalid(room) && (strcmp(where, "torture") == 0) && (kfx_config_state.conf.rules[plyr_idx].rooms.ghost_convert_chance > 0))
            becomes = torture_death_kind(thing, room);
        if (becomes > 0) value_init_string(value_dict_add(e, "on_death"), creature_code_name(becomes));
        if (strcmp(where, "torture") == 0) {
            VALUE *t = value_dict_add(e, "torture");
            value_init_dict(t);
            value_init_int64(value_dict_add(t, "turns_in"), (int64_t)get_gameturn() - (int64_t)cctrl->tortured.torturer_start_turn);
            value_init_int64(value_dict_add(t, "break_time"), creature_stats_get_from_thing(thing)->torture_break_time);
        }
        add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
        any = true;
    }
    // Chickens the hand could carry (mature ones, in the seat's own food rooms).
    int64_t oidx = kfx_sim_state.thing_lists[TngList_Objects].index;
    for (int64_t guard = 0; (oidx > 0) && (guard < 20000); guard++) {
        const struct Thing *thing = thing_get((ThingIndex)oidx);
        if (thing_is_invalid(thing)) break;
        oidx = thing->next_of_class;
        if (!object_is_mature_food(thing)) continue;
        const struct Room *room = get_room_thing_is_on(thing);
        if (room_is_invalid(room) || (room->owner != plyr_idx) || !room_role_matches(room->kind, RoRoF_FoodSpawn)) continue;
        if (food == NULL) { food = value_dict_add(cust, "food"); value_init_dict(food); }
        char key[16];
        snprintf(key, sizeof(key), "%d", (int)room->index);
        VALUE *f = value_dict_get(food, key);
        if (f == NULL) {
            f = value_dict_add(food, key);
            value_init_dict(f);
            value_init_int64(value_dict_add(f, "chickens"), 0);
            value_init_array(value_dict_add(f, "ids"));
        }
        VALUE *cnt = value_dict_get(f, "chickens");
        const int64_t c = value_int64(cnt) + 1;
        value_fini(cnt);
        value_init_int64(cnt, c);
        VALUE *ids = value_dict_get(f, "ids");
        if (value_array_size(ids) < 8) value_init_int32(value_array_append(ids), (int32_t)thing->index);
    }
    if (any) {
        VALUE *slot = value_dict_add(own, "custody");
        *slot = cust_real; // ownership moves into the view
    } else {
        value_fini(cust);
    }
}

// Graveyard and temple (docs/refactor/AI/omissions/06). The graveyard: bodies in it, progress toward the next vampire,
// bodies lying where the seat can see them. The temple: its efficiency, where its pool is (a creature dropped there is
// sacrificed), who is praying; the creatures offered so far and the recipes seen firing; and, while the seat owns a
// temple, the active recipe table (rules[0], the table the engine checks against for every player).
static void add_graveyard_and_temples(VALUE *out, VALUE *own, PlayerNumber plyr_idx, const struct Dungeon *dungeon)
{
    VALUE *gy = NULL, *temples = NULL;
    for (int64_t rkind = 1; rkind < TERRAIN_ITEMS_MAX; rkind++) {
        const TbBool grave = room_role_matches((RoomKind)rkind, RoRoF_DeadStorage);
        const TbBool temple = room_role_matches((RoomKind)rkind, RoRoF_CrSacrifice);
        if (!grave && !temple) continue;
        int64_t ridx = dungeon->room_list_start[rkind];
        for (int64_t guard = 0; (ridx > 0) && (guard < VIEW_LIST_CAP); guard++) {
            const struct Room *room = room_get((RoomIndex)ridx);
            if (room_is_invalid(room)) break;
            ridx = room->next_of_owner;
            if (grave) {
                if (gy == NULL) {
                    gy = value_dict_add(own, "graveyard");
                    value_init_dict(gy);
                    value_init_array(value_dict_add(gy, "rooms"));
                }
                VALUE *e = value_array_append(value_dict_get(gy, "rooms"));
                value_init_dict(e);
                value_init_int32(value_dict_add(e, "id"), (int32_t)room->index);
                value_init_int64(value_dict_add(e, "bodies"), room->used_capacity);
                value_init_int64(value_dict_add(e, "capacity"), room->total_capacity);
            }
            if (temple) {
                if (temples == NULL) { temples = value_dict_add(own, "temples"); value_init_array(temples); }
                VALUE *e = value_array_append(temples);
                value_init_dict(e);
                value_init_int32(value_dict_add(e, "id"), (int32_t)room->index);
                value_init_int32(value_dict_add(e, "efficiency"), (int32_t)(room->efficiency * 100 / ROOM_EFFICIENCY_MAX));
                TbBool found = false;
                SlabCodedCoords sn = room->slabs_list;
                for (int64_t g2 = 0; (sn != 0) && (g2 < VIEW_LIST_CAP) && !found; g2++) {
                    for (int k = 0; (k < 9) && !found; k++) {
                        const MapSubtlCoord x = slab_subtile(slb_num_decode_x(sn), k % 3), y = slab_subtile(slb_num_decode_y(sn), k / 3);
                        if (subtile_has_sacrificial_on_top(x, y) && can_drop_thing_here(x, y, plyr_idx, 0)) {
                            add_pos(e, "pool", x, y);
                            found = true;
                        }
                    }
                    sn = get_next_slab_number_in_room(sn);
                }
                int64_t praying = 0;
                int64_t ci = room->creatures_list;
                for (int64_t g3 = 0; (ci > 0) && (g3 < VIEW_LIST_CAP); g3++) {
                    const struct Thing *t = thing_get((ThingIndex)ci);
                    if (thing_is_invalid(t) || !thing_is_creature(t)) break;
                    praying++;
                    ci = creature_control_get_from_thing(t)->next_in_room;
                }
                value_init_int64(value_dict_add(e, "praying"), praying);
            }
        }
    }
    if (gy != NULL) {
        value_init_int64(value_dict_add(gy, "toward_vampire"), (int64_t)dungeon->bodies_rotten_for_vampire);
        value_init_int64(value_dict_add(gy, "per_vampire"), (int64_t)kfx_config_state.conf.rules[plyr_idx].rooms.bodies_for_vampire);
        int64_t lying = 0;
        int64_t idx = kfx_sim_state.thing_lists[TngList_DeadCreatrs].index;
        for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
            const struct Thing *t = thing_get((ThingIndex)idx);
            if (thing_is_invalid(t)) break;
            idx = t->next_of_class;
            if (!subtile_revealed(t->mappos.x.stl.num, t->mappos.y.stl.num, plyr_idx)) continue;
            const struct Room *room = get_room_thing_is_on(t);
            if (!room_is_invalid(room) && room_role_matches(room->kind, RoRoF_DeadStorage)) continue;
            lying++;
        }
        value_init_int64(value_dict_add(gy, "bodies_lying"), lying);
    }
    // What has been offered (counts per kind, since the last recipe that used them), and what came of it.
    VALUE *sac = value_dict_add(own, "sacrifices");
    value_init_dict(sac);
    VALUE *offered = value_dict_add(sac, "offered");
    value_init_dict(offered);
    const int64_t nmodels = kfx_config_state.conf.crtr_conf.model_count;
    for (int64_t m = 1; (m < CREATURE_TYPES_MAX) && (m <= nmodels); m++) {
        if (dungeon->creature_sacrifice[m] > 0) value_init_int64(value_dict_add(offered, creature_code_name((ThingModel)m)), dungeon->creature_sacrifice[m]);
    }
    api_seat_decision_add_sacrifice_outcomes(sac, plyr_idx);
    // The active recipes, as a human who read the rules file knows them -- only while the seat owns a temple.
    if (temples != NULL) {
        VALUE *rules = value_dict_get(out, "rules");
        if (rules != NULL) {
            VALUE *list = value_dict_add(rules, "sacrifices");
            value_init_array(list);
            for (int r = 0; r < MAX_SACRIFICE_RECIPES; r++) {
                const struct SacrificeRecipe *rc = &kfx_config_state.conf.rules[0].sacrifices.sacrifice_recipes[r];
                if (rc->action == SacA_None) continue;
                char text[160];
                api_seat_recipe_text(rc, text, sizeof(text));
                value_init_string(value_array_append(list), text);
            }
        }
    }
}

// Lasting powers the seat has running, which power_off can end early.
static void add_active_powers(VALUE *own, PlayerNumber plyr_idx)
{
    VALUE *ap = value_dict_add(own, "active_powers");
    value_init_dict(ap);
    if (player_uses_power_call_to_arms(plyr_idx)) {
        const struct PlayerInfo *player = get_player(plyr_idx);
        const struct Thing *flag = thing_get(player->cta_flag_idx);
        VALUE *c = value_dict_add(ap, "POWER_CALL_TO_ARMS");
        value_init_dict(c);
        if (!thing_is_invalid(flag)) add_pos(c, "pos", flag->mappos.x.stl.num, flag->mappos.y.stl.num);
    }
    if (player_uses_power_sight(plyr_idx)) { VALUE *c = value_dict_add(ap, "POWER_SIGHT"); value_init_dict(c); }
    if (player_uses_power_obey(plyr_idx)) { VALUE *c = value_dict_add(ap, "POWER_OBEY"); value_init_dict(c); }
}

// Things lying about that the hand can use: dungeon special boxes the seat can see (and whether its hand can reach
// them), with its dead creatures once a resurrect box is among them; and loose gold outside its treasure rooms.
#define LOOSE_GOLD_LISTED 16
static void add_pickups(VALUE *own, PlayerNumber plyr_idx, const struct Dungeon *dungeon)
{
    VALUE *specials = NULL, *gold = NULL;
    TbBool resurrect = false;
    int64_t gold_total = 0, gold_piles = 0;
    int64_t idx = kfx_sim_state.thing_lists[TngList_Objects].index;
    for (int64_t guard = 0; (idx > 0) && (guard < 20000); guard++) {
        struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        if (!subtile_revealed(thing->mappos.x.stl.num, thing->mappos.y.stl.num, plyr_idx)) continue;
        if (thing_is_special_box(thing)) {
            if (specials == NULL) { specials = value_dict_add(own, "specials"); value_init_array(specials); }
            VALUE *e = value_array_append(specials);
            value_init_dict(e);
            const SpecialKind spk = box_thing_to_special(thing);
            value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
            value_init_string(value_dict_add(e, "kind"), get_special_model_stats(spk)->code_name);
            value_init_bool(value_dict_add(e, "reachable"), can_thing_be_picked_up_by_player(thing, plyr_idx));
            add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
            if (spk == SpcKind_Resurrect) resurrect = true;
            continue;
        }
        if (object_is_gold(thing)) {
            const struct Room *room = get_room_thing_is_on(thing);
            if (!room_is_invalid(room) && room_role_matches(room->kind, RoRoF_GoldStorage)) continue;
            gold_piles++;
            gold_total += thing->valuable.gold_stored;
            if (gold == NULL) { gold = value_dict_add(own, "loose_gold"); value_init_dict(gold); value_init_array(value_dict_add(gold, "piles")); }
            if (value_array_size(value_dict_get(gold, "piles")) < LOOSE_GOLD_LISTED) {
                VALUE *e = value_array_append(value_dict_get(gold, "piles"));
                value_init_dict(e);
                value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
                value_init_int64(value_dict_add(e, "gold"), thing->valuable.gold_stored);
                value_init_bool(value_dict_add(e, "reachable"), can_thing_be_picked_up_by_player(thing, plyr_idx));
                add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
            }
        }
    }
    if (gold != NULL) {
        value_init_int64(value_dict_add(gold, "total"), gold_total);
        value_init_int64(value_dict_add(gold, "count"), gold_piles);
    }
    if (resurrect) {
        VALUE *dead = value_dict_add(own, "dead_creatures");
        value_init_array(dead);
        for (int64_t i = 0; (i < dungeon->dead_creatures_count) && (i < DEAD_CREATURES_MAX_COUNT); i++) {
            VALUE *e = value_array_append(dead);
            value_init_dict(e);
            value_init_string(value_dict_add(e, "kind"), creature_code_name(dungeon->dead_creatures[i].model));
            value_init_int32(value_dict_add(e, "level"), (int32_t)dungeon->dead_creatures[i].exp_level + 1);
        }
    }
}

// Chat, as the players see it: the last messages delivered, anyone's.
static void add_chat(VALUE *out)
{
    struct ExtSeatChat log[EXTSEAT_CHAT_LOG];
    const int64_t n = extseat_chat_log(log, EXTSEAT_CHAT_LOG);
    VALUE *chat = value_dict_add(out, "chat");
    value_init_array(chat);
    for (int64_t i = 0; i < n; i++) {
        VALUE *e = value_array_append(chat);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "player"), (int32_t)log[i].player);
        value_init_string(value_dict_add(e, "text"), log[i].text);
        value_init_int64(value_dict_add(e, "turn"), log[i].turn);
    }
}

// Everything owned by someone else that the seat can currently see.
static void add_visible(VALUE *out, PlayerNumber plyr_idx)
{
    VALUE *vis = value_dict_add(out, "visible");
    value_init_dict(vis);

    VALUE *crs = value_dict_add(vis, "creatures");
    value_init_array(crs);
    struct ForceTally by_owner[PLAYERS_COUNT];
    memset(by_owner, 0, sizeof(by_owner));
    int64_t idx = kfx_sim_state.thing_lists[TngList_Creatures].index;
    for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
        const struct Thing *thing = thing_get((ThingIndex)idx);
        if (thing_is_invalid(thing)) break;
        idx = thing->next_of_class;
        if (!thing_is_creature(thing) || (thing->owner == plyr_idx) || !creature_visible_to(thing, plyr_idx)) continue;
        const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
        VALUE *e = value_array_append(crs);
        value_init_dict(e);
        value_init_int32(value_dict_add(e, "id"), (int32_t)thing->index);
        add_owner_fields(e, thing, plyr_idx);
        value_init_string(value_dict_add(e, "kind"), creature_code_name(thing->model));
        value_init_int32(value_dict_add(e, "level"), (int32_t)(cctrl->exp_level + 1));
        value_init_int32(value_dict_add(e, "health"), (int32_t)thing->health);
        const TbBool digger = thing_is_creature_digger(thing);
        value_init_bool(value_dict_add(e, "digger"), digger);
        add_pos(e, "pos", thing->mappos.x.stl.num, thing->mappos.y.stl.num);
        if (thing->owner < PLAYERS_COUNT) {
            struct ForceTally *t = &by_owner[thing->owner];
            if (digger) t->workers++;
            else { t->fighters++; t->score += creature_force_score(thing); }
        }
    }
    // Only what is visible: fog hides the rest, exactly as it does for a human.
    VALUE *fbo = value_dict_add(vis, "force_by_owner");
    value_init_dict(fbo);
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
        if ((by_owner[p].fighters == 0) && (by_owner[p].workers == 0)) continue;
        char key[8];
        snprintf(key, sizeof(key), "%d", (int)p);
        add_force(fbo, key, &by_owner[p]);
    }

    // A room is known as far as its slabs are: report how many of them the seat has seen.
    VALUE *rooms = value_dict_add(vis, "rooms");
    value_init_array(rooms);
    for (int64_t owner = 0; owner < DUNGEONS_COUNT; owner++) {
        if (owner == plyr_idx) continue;
        const struct Dungeon *dungeon = get_dungeon((PlayerNumber)owner);
        if (dungeon_invalid(dungeon)) continue;
        for (int64_t rkind = 1; rkind < TERRAIN_ITEMS_MAX; rkind++) {
            int64_t ridx = dungeon->room_list_start[rkind];
            for (int64_t guard = 0; (ridx > 0) && (guard < VIEW_LIST_CAP); guard++) {
                const struct Room *room = room_get((RoomIndex)ridx);
                if (room_is_invalid(room)) break;
                int64_t seen_slabs = 0;
                MapSubtlCoord seen_x = 0, seen_y = 0;
                SlabCodedCoords sn = room->slabs_list;
                for (int64_t g2 = 0; (sn > 0) && (g2 < VIEW_LIST_CAP); g2++) {
                    const MapSlabCoord sx = slb_num_decode_x(sn), sy = slb_num_decode_y(sn);
                    if (slab_revealed(sx, sy, plyr_idx)) {
                        if (seen_slabs == 0) { seen_x = slab_subtile_center(sx); seen_y = slab_subtile_center(sy); }
                        seen_slabs++;
                    }
                    sn = get_next_slab_number_in_room(sn);
                }
                ridx = room->next_of_owner;
                if (seen_slabs == 0) continue;
                VALUE *e = value_array_append(rooms);
                value_init_dict(e);
                value_init_int32(value_dict_add(e, "id"), (int32_t)room->index);
                value_init_int32(value_dict_add(e, "owner"), (int32_t)owner);
                value_init_bool(value_dict_add(e, "ally"), players_are_mutual_allies(plyr_idx, (PlayerNumber)owner));
                value_init_string(value_dict_add(e, "kind"), room_code_name((RoomKind)rkind));
                value_init_int32(value_dict_add(e, "slabs_seen"), (int32_t)seen_slabs);
                add_pos(e, "pos", seen_x, seen_y); // a subtile in the first seen slab
            }
        }
    }

    VALUE *traps = value_dict_add(vis, "traps");
    value_init_array(traps);
    VALUE *doors = value_dict_add(vis, "doors");
    value_init_array(doors);
    add_traps_and_doors(traps, doors, plyr_idx, false);
}

// ---- creature summary and per-kind profile ---------------------------------------------------------------------------

#define INTEL_MAX_KINDS 64

// Combat class relative to the strongest and toughest kinds in the loaded config, e.g. "heavy melee fighter".
static const char *combat_class(const struct CreatureModelConfig *c, int64_t max_strength, int64_t max_health)
{
    if ((c->model_flags & CMF_IsSpectator) != 0) return "spectator (does not fight)";
    if (c->strength * 10 >= max_strength * 7) return "heavy melee fighter";
    if (c->health * 10 >= max_health * 7) return "tough tank";
    if (c->flying) return "flying skirmisher";
    if (c->strength * 10 >= max_strength * 4) return "average fighter";
    return "weak fighter";
}

static void add_job_names(VALUE *arr, int64_t mask)
{
    value_init_array(arr);
    for (int bit = 0; bit < 62; bit++) {
        if ((mask & (1LL << bit)) == 0) continue;
        const char *name = creature_job_code_name((CreatureJob)(1LL << bit));
        if ((name != NULL) && (name[0] != '\0')) value_init_string(value_array_append(arr), name);
    }
}

static void add_creature_profile(VALUE *out, ThingModel model, int64_t mx_str, int64_t mx_hp)
{
    const struct CreatureModelConfig *c = creature_stats_get(model);
    if (creature_stats_invalid(c)) return;
    value_init_dict(out);
    value_init_int64(value_dict_add(out, "health"), c->health);
    value_init_int64(value_dict_add(out, "strength"), c->strength);
    value_init_int32(value_dict_add(out, "armour"), c->armour);
    value_init_int32(value_dict_add(out, "defence"), c->defense);
    value_init_int32(value_dict_add(out, "dexterity"), c->dexterity);
    value_init_int32(value_dict_add(out, "speed"), c->base_speed);
    value_init_bool(value_dict_add(out, "flying"), c->flying != 0);
    value_init_bool(value_dict_add(out, "evil"), (c->model_flags & CMF_IsEvil) != 0);
    value_init_int64(value_dict_add(out, "pay"), c->pay);
    value_init_int32(value_dict_add(out, "lair_size"), c->lair_size);
    value_init_int64(value_dict_add(out, "hunger_rate"), c->hunger_rate);
    value_init_int32(value_dict_add(out, "research_value"), c->research_value);
    value_init_int32(value_dict_add(out, "training_value"), c->training_value);
    value_init_int32(value_dict_add(out, "manufacture_value"), c->manufacture_value);
    VALUE *ab = value_dict_add(out, "abilities");
    value_init_array(ab);
    for (int64_t i = 0; i < LEARNED_INSTANCES_COUNT; i++) {
        if (c->learned_instance_id[i] == 0) continue;
        VALUE *e = value_array_append(ab);
        value_init_dict(e);
        value_init_string(value_dict_add(e, "name"), creature_instance_code_name(c->learned_instance_id[i]));
        value_init_int32(value_dict_add(e, "from_level"), c->learned_instance_level[i]);
    }
    // What it does for the dungeon (the jobs its own AI picks) and how it fights; the values above are the raw numbers.
    add_job_names(value_dict_add(out, "primary_jobs"), c->job_primary);
    add_job_names(value_dict_add(out, "secondary_jobs"), c->job_secondary);
    value_init_string(value_dict_add(out, "combat"), combat_class(c, mx_str, mx_hp));
}

static void add_creature_intel(VALUE *own, const struct Dungeon *dungeon)
{
    struct Kind { int64_t count, level_sum, level_max, hp_sum; int64_t levels[CREATURE_MAX_LEVEL + 1]; TbBool digger; } kinds[INTEL_MAX_KINDS];
    memset(kinds, 0, sizeof(kinds));
    const int64_t nmodels = kfx_config_state.conf.crtr_conf.model_count;
    for (int pass = 0; pass < 2; pass++) {
        int64_t idx = (pass == 0) ? dungeon->creatr_list_start : dungeon->digger_list_start;
        for (int64_t guard = 0; (idx > 0) && (guard < VIEW_LIST_CAP); guard++) {
            const struct Thing *thing = thing_get((ThingIndex)idx);
            if (thing_is_invalid(thing) || !thing_is_creature(thing)) break;
            const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
            if ((thing->model > 0) && (thing->model < INTEL_MAX_KINDS) && (thing->model < nmodels + 1)) {
                struct Kind *k = &kinds[thing->model];
                const int64_t lvl = cctrl->exp_level + 1;
                k->count++; k->level_sum += lvl; k->hp_sum += thing->health; k->digger = (pass == 1);
                if (lvl > k->level_max) k->level_max = lvl;
                if ((lvl >= 1) && (lvl <= CREATURE_MAX_LEVEL)) k->levels[lvl]++;
            }
            idx = cctrl->players_next_creature_idx;
        }
    }
    int64_t mx_str = 1, mx_hp = 1;
    for (int64_t m = 1; m <= nmodels; m++) {
        const struct CreatureModelConfig *c = creature_stats_get((ThingModel)m);
        if (creature_stats_invalid(c) || ((c->model_flags & CMF_IsSpectator) != 0)) continue;
        if (c->strength > mx_str) mx_str = c->strength;
        if (c->health > mx_hp) mx_hp = c->health;
    }
    VALUE *sum = value_dict_add(own, "creature_summary");
    value_init_dict(sum);
    VALUE *info = value_dict_add(own, "creature_info");
    value_init_dict(info);
    for (int64_t m = 1; (m < INTEL_MAX_KINDS) && (m <= nmodels); m++) {
        const struct Kind *k = &kinds[m];
        if (k->count == 0) continue;
        VALUE *e = value_dict_add(sum, creature_code_name((ThingModel)m));
        value_init_dict(e);
        value_init_int64(value_dict_add(e, "count"), k->count);
        value_init_int64(value_dict_add(e, "max_level"), k->level_max);
        value_init_int64(value_dict_add(e, "avg_level_x10"), k->level_sum * 10 / k->count);
        value_init_int64(value_dict_add(e, "avg_health"), k->hp_sum / k->count);
        value_init_bool(value_dict_add(e, "digger"), k->digger);
        VALUE *lv = value_dict_add(e, "by_level");
        value_init_dict(lv);
        for (int64_t l = 1; l <= CREATURE_MAX_LEVEL; l++) {
            if (k->levels[l] > 0) {
                char key[8];
                snprintf(key, sizeof(key), "%d", (int)l);
                value_init_int64(value_dict_add(lv, key), k->levels[l]);
            }
        }
        add_creature_profile(value_dict_add(info, creature_code_name((ThingModel)m)), (ThingModel)m, mx_str, mx_hp);
    }
}

void api_seat_build_view(VALUE *out, PlayerNumber plyr_idx)
{
    const struct PlayerInfo *player = get_player(plyr_idx);
    const struct Dungeon *dungeon = get_players_dungeon(player);
    value_init_dict(out);
    value_init_int64(value_dict_add(out, "turn"), (int64_t)get_gameturn());
    value_init_bool(value_dict_add(out, "paused"), flag_is_set(kfx_sim_state.operation_flags, GOF_Paused));
    value_init_bool(value_dict_add(out, "agent_pause"), extseat_agent_owns_pause());
    value_init_bool(value_dict_add(out, "advancing"), extseat_advancing());
    // The simulation's real-time pace (set_game_speed), global to the game, not per seat -- shown so an agent that
    // slowed the game down for itself to think can see the current rate, not just the value it last requested.
    value_init_int64(value_dict_add(out, "turns_per_second"), (int64_t)kfx_sim_state.turns_per_second);

    VALUE *seat = value_dict_add(out, "seat");
    value_init_dict(seat);
    value_init_int32(value_dict_add(seat, "player"), (int32_t)plyr_idx);
    value_init_int32(value_dict_add(seat, "user"), (int32_t)player->user_id);
    // Deliberately this seat's own fate: a decided seat does not end the shared game (see 06 section 3).
    value_init_string(value_dict_add(seat, "victory_state"), victory_name(player->victory_state));
    // How many steps of the current gesture are still to be written; 0 means submit_action will accept a new verb.
    value_init_int32(value_dict_add(seat, "queued_steps"), (int32_t)extseat_queued_steps(player->user_id));
    // Verbs waiting behind the running gesture (submit_action with queue=true), and what recently became of verbs:
    // "done" once their steps were written, "rejected" with a code if they went stale or failed when their turn came.
    value_init_int32(value_dict_add(seat, "queued_verbs"), (int32_t)extseat_queued_verbs(player->user_id));
    {
        struct ExtSeatResult res[EXTSEAT_RESULT_RING];
        const int64_t nres = extseat_results(player->user_id, res, EXTSEAT_RESULT_RING);
        VALUE *arr = value_dict_add(seat, "results");
        value_init_array(arr);
        for (int64_t i = 0; i < nres; i++) {
            VALUE *e = value_array_append(arr);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "id"), (int32_t)res[i].id);
            value_init_string(value_dict_add(e, "status"), res[i].rejected ? "rejected" : "done");
            if (res[i].rejected) value_init_string(value_dict_add(e, "error"), res[i].error);
            value_init_int64(value_dict_add(e, "turn"), res[i].turn);
        }
    }
    api_seat_decision_add_to_view(seat, plyr_idx);
    // Creatures sent somewhere with move_creature: how many are still held, and the ones the safety net handed back
    // (why: max_hold, payday, owed_pay, hungry, agent_lost) so the agent knows they are working again.
    value_init_int32(value_dict_add(seat, "ordered_creatures"), (int32_t)extseat_ordered_count(player->user_id));
    {
        struct ExtSeatAutoRelease ar[EXTSEAT_AUTO_RELEASE_RING];
        const int64_t nar = extseat_auto_releases(player->user_id, ar, EXTSEAT_AUTO_RELEASE_RING);
        VALUE *arr = value_dict_add(seat, "auto_released");
        value_init_array(arr);
        for (int64_t i = 0; i < nar; i++) {
            VALUE *e = value_array_append(arr);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "id"), (int32_t)ar[i].thing_id);
            value_init_string(value_dict_add(e, "reason"), ar[i].reason);
            value_init_int64(value_dict_add(e, "turn"), ar[i].turn);
        }
    }
    {
        // Pay day is the natural planning beat (the agent can decide once per quarter). Progress grows by
        // pay_day_speed/100 per turn and pays out at pay_day_gap.
        const struct GameRulesConfig *gp = &kfx_config_state.conf.rules[plyr_idx].gameplay;
        const int64_t gap = (int64_t)gp->pay_day_gap;
        const int64_t progress = (int64_t)kfx_config_state.pay_day_progress[plyr_idx];
        const int64_t per_turn = (int64_t)gp->pay_day_speed / 100;
        VALUE *pd = value_dict_add(seat, "payday");
        value_init_dict(pd);
        value_init_int64(value_dict_add(pd, "progress"), progress);
        value_init_int64(value_dict_add(pd, "gap"), gap);
        value_init_int32(value_dict_add(pd, "quarter"), (gap > 0) ? (int32_t)((4 * progress / gap) % 4) : 0);
        if ((gap > 0) && (per_turn > 0)) {
            const int64_t qlen = gap / 4;
            const int64_t next_q = (qlen > 0) ? ((progress / qlen) + 1) * qlen : gap;
            value_init_int64(value_dict_add(pd, "turns_to_payday"), (gap > progress) ? (gap - progress + per_turn - 1) / per_turn : 0);
            value_init_int64(value_dict_add(pd, "turns_to_next_quarter"), (next_q > progress) ? (next_q - progress + per_turn - 1) / per_turn : 0);
        }
    }

    {
        // The level being played, as the level-select screens name it (the spoken briefing is audio only).
        VALUE *lv = value_dict_add(out, "level");
        value_init_dict(lv);
        const LevelNumber lvnum = get_loaded_level_number();
        value_init_int64(value_dict_add(lv, "number"), (int64_t)lvnum);
        // The campaign or mappack the level comes from (its file name): rules, creatures and spells differ between them.
        if (campaign.fname[0] != 0) value_init_string(value_dict_add(lv, "campaign"), campaign.fname);
        if (campaign.display_name[0] != 0) value_init_string(value_dict_add(lv, "campaign_name"), campaign.display_name);
        const struct LevelInformation *lvinfo = get_level_info(lvnum);
        if (lvinfo != NULL) {
            value_init_string(value_dict_add(lv, "name"), (lvinfo->name_stridx > 0) ? get_string(lvinfo->name_stridx) : lvinfo->name);
            if (lvinfo->description[0] != 0) value_init_string(value_dict_add(lv, "description"), lvinfo->description);
        }
    }

    {
        // Who is in the level (docs/refactor/AI/omissions/09-persistent-memory.md section 6.8): what a human sees in the
        // player tooltips (the name) and, outside campaign levels, chose in skirmish setup (each computer keeper's AI).
        // In a campaign the level script picks the AI and no screen shows it, so it is left out there.
        VALUE *pls = value_dict_add(out, "players");
        value_init_array(pls);
        // Multiplayer and free-play maps are the ones skirmish setup (and the network lobby) offers, where the AI is
        // chosen on screen; a single-player, bonus or extra level's script picks it.
        const LevelNumber here = get_loaded_level_number();
        const TbBool campaign_level = is_singleplayer_level(here) || is_bonus_level(here) || is_extra_level(here);
        for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        {
            const struct PlayerInfo *pl = get_player(p);
            if (!player_exists(pl) || (p == PLAYER_NEUTRAL))
                continue;
            const char *kind = "human";
            if (p == PLAYER_GOOD) kind = "heroes";
            else if (flag_is_set(pl->allocflags, PlaF_ExternalSeat)) kind = "external";
            else if (flag_is_set(pl->allocflags, PlaF_CompCtrl)) kind = "computer";
            VALUE *e = value_array_append(pls);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "id"), (int32_t)p);
            value_init_int32(value_dict_add(e, "player"), (int32_t)p);
            value_init_string(value_dict_add(e, "kind"), kind);
            if (pl->player_name[0] != 0)
                value_init_string(value_dict_add(e, "name"), pl->player_name);
            if ((strcmp(kind, "computer") == 0) && !campaign_level)
            {
                const struct Computer2 *comp = get_computer_player(p);
                if (!computer_player_invalid(comp) && (comp->model < COMPUTER_MODELS_COUNT)
                 && (comp_player_conf.computer_types[comp->model].name[0] != 0))
                    value_init_string(value_dict_add(e, "ai_type"), comp_player_conf.computer_types[comp->model].name);
            }
            if (p != PLAYER_GOOD)
                value_init_bool(value_dict_add(e, "alive"), !thing_is_invalid(find_players_dungeon_heart(p)));
        }
    }

    {
        // Configured odds and thresholds: what the manual and the rules file tell a player (not hidden state).
        const struct RoomRulesConfig *rr = &kfx_config_state.conf.rules[plyr_idx].rooms;
        VALUE *rules = value_dict_add(out, "rules");
        value_init_dict(rules);
        VALUE *c = value_dict_add(rules, "custody");
        value_init_dict(c);
        value_init_int32(value_dict_add(c, "prison_skeleton_chance"), rr->prison_skeleton_chance);
        value_init_int32(value_dict_add(c, "torture_convert_chance"), rr->torture_convert_chance);
        value_init_int32(value_dict_add(c, "torture_death_chance"), rr->torture_death_chance);
        value_init_int32(value_dict_add(c, "ghost_convert_chance"), rr->ghost_convert_chance);
        value_init_int64(value_dict_add(c, "turns_per_torture_health_loss"), kfx_config_state.conf.rules[plyr_idx].health.turns_per_torture_health_loss);
        value_init_int32(value_dict_add(c, "max_things_in_hand"), kfx_config_state.conf.rules[plyr_idx].gameplay.max_things_in_hand);
    }

    VALUE *own = value_dict_add(out, "own");
    value_init_dict(own);
    if (dungeon_invalid(dungeon)) {
        return;
    }
    value_init_int64(value_dict_add(own, "gold"), (int64_t)dungeon->total_money_owned);
    // The level's standing brief as the objective box shows it (every DISPLAY_OBJECTIVE / QUICK_OBJECTIVE lands in
    // evntbox_text_objective), and the ones before it, since the sim keeps only the latest.
    value_init_string(value_dict_add(own, "objective"), kfx_sim_state.evntbox_text_objective[plyr_idx]);
    api_seat_decision_add_objectives(own, plyr_idx);

    VALUE *crs = value_dict_add(own, "creatures");
    value_init_array(crs);
    add_creature_list(crs, dungeon->creatr_list_start, false);
    add_creature_list(crs, dungeon->digger_list_start, true);

    VALUE *rooms = value_dict_add(own, "rooms");
    value_init_array(rooms);
    for (int64_t rkind = 1; rkind < TERRAIN_ITEMS_MAX; rkind++) {
        int64_t ridx = dungeon->room_list_start[rkind];
        for (int64_t guard = 0; (ridx > 0) && (guard < VIEW_LIST_CAP); guard++) {
            const struct Room *room = room_get((RoomIndex)ridx);
            if (room_is_invalid(room)) {
                break;
            }
            VALUE *e = value_array_append(rooms);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "id"), (int32_t)room->index);
            value_init_string(value_dict_add(e, "kind"), room_code_name((RoomKind)rkind));
            value_init_int32(value_dict_add(e, "slabs"), (int32_t)room->slabs_count);
            add_pos(e, "pos", room->central_stl_x, room->central_stl_y);
            add_room_quality(e, room);
            ridx = room->next_of_owner;
        }
    }

    VALUE *stock = value_dict_add(own, "stock");
    value_init_dict(stock);
    VALUE *traps = value_dict_add(stock, "traps");
    value_init_dict(traps);
    for (int64_t m = 1; m < kfx_config_state.conf.trapdoor_conf.trap_types_count; m++) {
        if (dungeon->mnfct_info.trap_amount_placeable[m] > 0) {
            value_init_int32(value_dict_add(traps, trap_code_name(m)), dungeon->mnfct_info.trap_amount_placeable[m]);
        }
    }
    VALUE *doors = value_dict_add(stock, "doors");
    value_init_dict(doors);
    for (int64_t m = 1; m < kfx_config_state.conf.trapdoor_conf.door_types_count; m++) {
        if (dungeon->mnfct_info.door_amount_placeable[m] > 0) {
            value_init_int32(value_dict_add(doors, door_code_name(m)), dungeon->mnfct_info.door_amount_placeable[m]);
        }
    }

    VALUE *powers = value_dict_add(own, "powers");
    value_init_array(powers);
    for (int64_t pk = 1; pk < kfx_config_state.conf.magic_conf.power_types_count; pk++) {
        if (is_power_available(plyr_idx, (PowerKind)pk)) {
            value_init_string(value_array_append(powers), power_code_name((PowerKind)pk));
        }
    }

    // What research has unlocked (research is the creatures' job; the agent only needs to know the result): rooms it may
    // build, traps and doors it may manufacture, powers it may cast (own.powers above). Stock is in own.stock.
    {
        VALUE *av = value_dict_add(own, "unlocked");
        value_init_dict(av);
        VALUE *ar = value_dict_add(av, "rooms");
        value_init_array(ar);
        for (int64_t rk = 1; rk < TERRAIN_ITEMS_MAX; rk++) {
            if (is_room_available(plyr_idx, (RoomKind)rk)) value_init_string(value_array_append(ar), room_code_name((RoomKind)rk));
        }
        // Gold per slab of each unlocked room, so build_room's cost (area x this) is computable without a submit round
        // trip -- the same reason own.power_costs exists for cast_power. A flat per-slab rate (roomspace.c's own
        // affordability check: slab_count * roomst->cost <= total_money_owned), not progressive with room size.
        VALUE *rc = value_dict_add(own, "room_costs");
        value_init_dict(rc);
        for (int64_t rk = 1; rk < TERRAIN_ITEMS_MAX; rk++) {
            if (is_room_available(plyr_idx, (RoomKind)rk)) {
                value_init_int64(value_dict_add(rc, room_code_name((RoomKind)rk)), get_room_kind_stats((RoomKind)rk)->cost);
            }
        }
        VALUE *at = value_dict_add(av, "traps");
        value_init_array(at);
        for (int64_t m = 1; m < kfx_config_state.conf.trapdoor_conf.trap_types_count; m++) {
            if (is_trap_buildable(plyr_idx, m)) value_init_string(value_array_append(at), trap_code_name(m));
        }
        VALUE *ad = value_dict_add(av, "doors");
        value_init_array(ad);
        for (int64_t m = 1; m < kfx_config_state.conf.trapdoor_conf.door_types_count; m++) {
            if (is_door_buildable(plyr_idx, m)) value_init_string(value_array_append(ad), door_code_name(m));
        }
    }

    // Creature tendencies (set_tendency): imprison captured enemies instead of killing them, and let hurt creatures flee.
    {
        VALUE *td = value_dict_add(own, "tendencies");
        value_init_dict(td);
        value_init_bool(value_dict_add(td, "imprison"), (dungeon->creature_tendencies & CrTend_Imprison) != 0);
        value_init_bool(value_dict_add(td, "flee"), (dungeon->creature_tendencies & CrTend_Flee) != 0);
    }
    add_custody(own, plyr_idx, dungeon);
    add_graveyard_and_temples(out, own, plyr_idx, dungeon);

    // Research is the creatures' job, but "what's next and how far off" is what an agent needs to plan two steps
    // ahead, not just the already-unlocked result (own.unlocked): dungeon->research[] is the ordered queue,
    // current_research_idx the item in progress, research_progress its accumulated points (both in <<8 fixed point,
    // hence the shift back before dividing) against that item's req_amount.
    {
        VALUE *rs = value_dict_add(own, "research");
        value_init_dict(rs);
        if ((dungeon->current_research_idx >= 0) && (dungeon->current_research_idx < DUNGEON_RESEARCH_COUNT)) {
            const struct ResearchVal *cur = &dungeon->research[dungeon->current_research_idx];
            const char *cat = (cur->rtyp == RsCat_Room) ? "room" : (cur->rtyp == RsCat_Power) ? "power"
                : (cur->rtyp == RsCat_Creature) ? "creature" : "other";
            const char *name = (cur->rtyp == RsCat_Room) ? room_code_name((RoomKind)cur->rkind)
                : (cur->rtyp == RsCat_Power) ? power_code_name((PowerKind)cur->rkind)
                : (cur->rtyp == RsCat_Creature) ? creature_code_name((ThingModel)cur->rkind) : "?";
            const int64_t need = cur->req_amount << 8;
            VALUE *c = value_dict_add(rs, "current");
            value_init_dict(c);
            value_init_string(value_dict_add(c, "category"), cat);
            value_init_string(value_dict_add(c, "name"), name);
            value_init_int32(value_dict_add(c, "progress_pct"), (need > 0) ? (int32_t)(dungeon->research_progress * 100 / need) : 0);
        }
        VALUE *q = value_dict_add(rs, "queue");
        value_init_array(q);
        for (int64_t i = 0; i < dungeon->research_num; i++) {
            const struct ResearchVal *r = &dungeon->research[i];
            if (!research_needed(r, dungeon)) continue;
            const char *name = (r->rtyp == RsCat_Room) ? room_code_name((RoomKind)r->rkind)
                : (r->rtyp == RsCat_Power) ? power_code_name((PowerKind)r->rkind)
                : (r->rtyp == RsCat_Creature) ? creature_code_name((ThingModel)r->rkind) : "?";
            value_init_string(value_array_append(q), name);
        }
    }

    // Alliances (set_alliance): `declared` is who this seat has flagged as an ally (a one-way declaration, exactly what
    // the human alliance button does); `mutual` is the subset where the other side has declared back, which is what
    // the engine's own combat/vision-sharing rules actually check (players_are_mutual_allies).
    {
        VALUE *al = value_dict_add(own, "alliance");
        value_init_dict(al);
        VALUE *decl = value_dict_add(al, "declared");
        value_init_array(decl);
        VALUE *mut = value_dict_add(al, "mutual");
        value_init_array(mut);
        for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++) {
            if (p == plyr_idx) continue;
            if (player_allied_with(player, p)) value_init_int32(value_array_append(decl), (int32_t)p);
            if (players_are_mutual_allies(plyr_idx, p)) value_init_int32(value_array_append(mut), (int32_t)p);
        }
    }

    // The army at a glance, per creature kind, and (once per kind owned) what that kind is good at.
    add_creature_intel(own, dungeon);
    {
        // Workforce vs army: the digger list is the workforce; everything else fights.
        struct ForceTally t;
        memset(&t, 0, sizeof(t));
        for (int pass = 0; pass < 2; pass++) {
            int64_t cidx = (pass == 0) ? dungeon->creatr_list_start : dungeon->digger_list_start;
            for (int64_t guard = 0; (cidx > 0) && (guard < VIEW_LIST_CAP); guard++) {
                const struct Thing *thing = thing_get((ThingIndex)cidx);
                if (thing_is_invalid(thing) || !thing_is_creature(thing)) break;
                if (pass == 1) t.workers++;
                else { t.fighters++; t.score += creature_force_score(thing); }
                cidx = creature_control_get_from_thing(thing)->players_next_creature_idx;
            }
        }
        add_force(own, "force", &t);
    }

    // Gold cost of each available power at each charge level (level = held turns / 4, capped), so the agent can budget a
    // cast and know what an overcharge costs. Some powers charge only up to a lower level; the costs repeat past it.
    VALUE *costs = value_dict_add(own, "power_costs");
    value_init_dict(costs);
    for (int64_t pk = 1; pk < kfx_config_state.conf.magic_conf.power_types_count; pk++) {
        if (!is_power_available(plyr_idx, (PowerKind)pk)) continue;
        VALUE *arr = value_dict_add(costs, power_code_name((PowerKind)pk));
        value_init_array(arr);
        for (int64_t lvl = 0; lvl < MAGIC_OVERCHARGE_LEVELS; lvl++) {
            value_init_int64(value_array_append(arr), (int64_t)compute_power_price(plyr_idx, (PowerKind)pk, (KeepPwrLevel)lvl));
        }
    }

    // Event markers: the panel's notification buttons for this seat (attacks, new rooms/creatures, pay day, breaches...).
    // `target` is the engine's raw value (a creature or room index for most kinds); `lifespan` is turns left, if it expires.
    {
        static const char *const names[] = {
            "nothing", "heart_attacked", "enemy_fight", "objective", "breach", "new_room_research", "new_creature",
            "new_spell_research", "new_trap", "new_door", "creature_scavenged", "treasure_room_full", "creature_payday",
            "area_discovered", "spell_picked_up", "room_taken_over", "creature_annoyed", "no_more_living_set",
            "alarm_triggered", "room_under_attack", "need_treasure_room", "information", "room_lost", "creature_hungry",
            "trap_crate_found", "door_crate_found", "special_found", "quick_information", "friendly_fight",
            "work_room_unreachable", "storage_room_unreachable", "prisoner_starving", "tortured_hurt", "enemy_door",
            "secret_door_discovered", "secret_door_spotted" };
        VALUE *evs = value_dict_add(own, "events");
        value_init_array(evs);
        for (int64_t i = 0; i < EVENTS_COUNT; i++) {
            const struct Event *ev = &kfx_sim_state.event[i];
            if (!flag_is_set(ev->flags, EvF_Exists) || (ev->owner != plyr_idx)) continue;
            VALUE *e = value_array_append(evs);
            value_init_dict(e);
            value_init_int32(value_dict_add(e, "id"), (int32_t)ev->index);
            value_init_string(value_dict_add(e, "kind"), (ev->kind < sizeof(names) / sizeof(names[0])) ? names[ev->kind] : "unknown");
            add_pos(e, "pos", ev->mappos_x >> 8, ev->mappos_y >> 8);
            value_init_int64(value_dict_add(e, "target"), ev->target);
            if (ev->lifespan_turns > 0) value_init_int64(value_dict_add(e, "lifespan"), (int64_t)ev->lifespan_turns);
            // What the event box says when a human opens it (the information and objective messages especially).
            char text[MESSAGE_TEXT_LEN];
            event_text_for(ev, plyr_idx, text, sizeof(text));
            if (text[0] != 0) value_init_string(value_dict_add(e, "text"), text);
        }
    }

    // The slabs the seat has marked for digging (the engine keeps at most MAPTASKS_COUNT of them), so an agent can
    // see that a mark_dig took and how much room it has left.
    VALUE *marks = value_dict_add(own, "dig_marks");
    value_init_array(marks);
    int64_t marks_used = dungeon->highest_task_number;
    if (marks_used > MAPTASKS_COUNT) marks_used = MAPTASKS_COUNT;
    for (int64_t i = 0; i < marks_used; i++) {
        const struct MapTask *task = &dungeon->task_list[i];
        if (task->kind == 0) continue;
        VALUE *e = value_array_append(marks);
        value_init_array(e);
        value_init_int32(value_array_append(e), (int32_t)subtile_slab(stl_num_decode_x(task->coords)));
        value_init_int32(value_array_append(e), (int32_t)subtile_slab(stl_num_decode_y(task->coords)));
    }
    value_init_int32(value_dict_add(own, "dig_marks_limit"), MAPTASKS_COUNT);
    // Marks no imp can get to (a corridor that does not touch walkable ground): they sit there undug forever.
    int64_t unreach_xy[2 * 64];
    const int64_t unreach = api_seat_unreachable_dig_slabs(plyr_idx, NULL, 0, NULL, unreach_xy, 64);
    VALUE *unreach_v = value_dict_add(own, "unreachable_dig_marks");
    value_init_array(unreach_v);
    for (int64_t i = 0; (i < unreach) && (i < 64); i++) {
        VALUE *e = value_array_append(unreach_v);
        value_init_array(e);
        value_init_int32(value_array_append(e), (int32_t)unreach_xy[2 * i]);
        value_init_int32(value_array_append(e), (int32_t)unreach_xy[2 * i + 1]);
    }
    value_init_int32(value_dict_add(own, "unreachable_dig_marks_count"), (int32_t)unreach);

    VALUE *own_traps = value_dict_add(own, "traps");
    value_init_array(own_traps);
    VALUE *own_doors = value_dict_add(own, "doors");
    value_init_array(own_doors);
    add_traps_and_doors(own_traps, own_doors, plyr_idx, true);
    add_active_powers(own, plyr_idx);
    add_pickups(own, plyr_idx, dungeon);
    add_chat(out);

    add_map(out, plyr_idx);
    add_visible(out, plyr_idx);
}
