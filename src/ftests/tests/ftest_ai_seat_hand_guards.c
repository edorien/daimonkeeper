// What an External seat's hand drop does besides moving a creature, refused unless the order says it means it, and what a
// sell of library slabs does to the spells on their shelves. Checks (dry runs): one of the seat's own creatures dropped in
// its prison is jailed (WOULD_IMPRISON; imprison: true accepts; imprison off a prison is NOT_A_PRISON), and one dropped
// in its torture chamber goes on the rack (WOULD_TORTURE; torture: true; NOT_A_TORTURE_CHAMBER) -- but not a dark
// mistress, whose kinky torture is her own choice, nor an imp; any creature
// dropped anywhere on its portal -- the centre and a border subtile alike, by pos or to_room -- is dismissed
// (WOULD_DISMISS; dismiss: true accepts; dismiss off a portal is NOT_A_PORTAL), but an imp is not; selling the only
// library while it holds spellbooks is WOULD_LOSE_SPELLS (lose_spells: true accepts), and is accepted once another library
// has shelf space for them. Then for real: a fly dropped with dismiss: true on a border subtile of the portal leaves the
// dungeon, an orc dropped with imprison: true lands in captivity and one dropped with torture: true on the rack -- the
// engine rule the refusals predict. Last, a
// six-slab bridge the seat builds over lava (as on keeporig:5): every subtile of each bridge slab is probed with
// can_place_thing_here for big and flying creatures as the slabs appear, and eight creatures are dropped on the
// bridge's first slab in one order -- while a drop sent before the bridge's slabs exist is refused CANNOT_DROP_HERE.
#include "ftest_ai_seat_hand_guards.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_creature.h"
#include "config_keeperfx.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "creature_control.h"
#include "creature_states.h"
#include "dungeon_data.h"
#include "external_seat.h"
#include "frontend.h"
#include "game_legacy.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "map_columns.h"
#include "net_game.h"
#include "player_availability.h"
#include "player_data.h"
#include "power_hand.h"
#include "room_data.h"
#include "room_library.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_list.h"
#include "thing_physics.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)
#define CHECK_TRUE(what, cond) do { if (!(cond)) SOFT_FAIL("%s", what); } while (0)

#define BRIDGE_LEN 6
#define GROUP_MAX 8
static PlayerNumber P = -1;
static NetUserId U = -1;
static MapSlabCoord hsx, hsy;
static RoomIndex s_prison = 0, s_portal = 0, s_library = 0, s_torture = 0;
static ThingIndex s_orc = 0, s_imp = 0, s_fly = 0, s_jail = 0, s_rack = 0, s_mistress = 0;
static GameTurn s_fly_born = 0;
static ThingIndex s_group[GROUP_MAX];
static int64_t s_group_n = 0;
static int64_t s_ids[4];
static int64_t s_books = 0;
static int64_t s_slab_ready_turn[BRIDGE_LEN];
static int64_t s_probe_failures = 0;

/** The layout is laid out northwards from the heart: Y(n) is n slabs north of it. */
#define Y(n) (hsy - (n))
static void fill(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1, SlabKind kind, PlayerNumber owner)
{
    ftest_util_replace_slabs(min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), kind, owner);
}
static struct ExtSeatVerb carry(ThingIndex t)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop; v.has_thing = true; v.thing_id = t;
    return v;
}
static struct ExtSeatVerb at(struct ExtSeatVerb v, MapSubtlCoord x, MapSubtlCoord y) { v.has_pos = true; v.stl_x = x; v.stl_y = y; return v; }
static struct ExtSeatVerb into(struct ExtSeatVerb v, RoomIndex room) { v.has_room = true; v.room_id = room; return v; }
static struct ExtSeatVerb sell(MapSlabCoord x0, MapSlabCoord y0, MapSlabCoord x1, MapSlabCoord y1)
{
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_Sell; v.has_rect = true; v.slab_x0 = x0; v.slab_y0 = y0; v.slab_x1 = x1; v.slab_y1 = y1;
    return v;
}
static void expect(const char* what, struct ExtSeatVerb v, const char* code)
{
    int64_t steps = 0;
    const char* e = extseat_check_verb(U, P, &v, &steps);
    if ((code == NULL) != (e == NULL) || (code != NULL && strcmp(e, code) != 0))
        SOFT_FAIL("%s: expected %s, got %s", what, code ? code : "success", e ? e : "success");
}
static const struct ExtSeatResult* result_for(int64_t id)
{
    static struct ExtSeatResult res[EXTSEAT_RESULT_RING];
    const int64_t n = extseat_results(U, res, EXTSEAT_RESULT_RING);
    for (int64_t k = 0; k < n; k++) if (res[k].id == id) return &res[k];
    return NULL;
}
static const char* submit(struct ExtSeatVerb v, int64_t* id)
{
    struct ExtSeatSubmitInfo info; memset(&info, 0, sizeof(info));
    const char* e = extseat_submit_verb_ex(U, P, &v, true, &info);
    if (id) *id = info.id;
    return e;
}
static struct Thing* make_creature(const char* kind, MapSlabCoord sx, MapSlabCoord sy, CrtrExpLevel level)
{
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, sx, sy);
    struct Thing* t = ftest_util_create_creature(pos.x.val, pos.y.val, P, 1, (ThingModel)creature_model_id(kind));
    if (thing_is_invalid(t)) return NULL;
    set_creature_level(t, level);
    return t;
}
static TbBool bridge_slab_built(int64_t i)
{
    const struct SlabMap* s = get_slabmap_block(hsx + 1, Y(8 + i));
    return (s->kind == SlbT_BRIDGE) && (slabmap_owner(s) == P);
}
/** can_place_thing_here for every creature of the bridge group at one subtile; a refusal is logged with what failed. */
static int64_t probe_subtile(MapSubtlCoord x, MapSubtlCoord y)
{
    int64_t bad = 0;
    for (int64_t g = 0; g < s_group_n; g++) {
        struct Thing* t = thing_get(s_group[g]);
        if (!thing_is_creature(t) || can_place_thing_here(t, x, y, P)) continue;
        struct Coord3d pos = t->mappos;
        pos.x.val = subtile_coord_center(x); pos.y.val = subtile_coord_center(y);
        pos.z.val = get_thing_height_at(t, &pos);
        const struct Map* mb = get_map_block_at(x, y);
        FTESTLOG("probe: %s %d refused at (%d,%d) turn %d: drop %d, z %d (now %d), floor %d, ceiling %d, in wall %d",
            thing_model_name(t), (int)t->index, (int)x, (int)y, (int)get_gameturn(),
            (int)can_drop_thing_here(x, y, P, 0), (int)pos.z.val, (int)t->mappos.z.val,
            (int)get_map_floor_height(mb), (int)get_map_ceiling_height(mb), (int)thing_in_wall_at(t, &pos));
        bad++;
    }
    return bad;
}

FTestActionResult hg01_setup(struct FTestActionArgs* const args);
FTestActionResult hg02_guards_then_for_real(struct FTestActionArgs* const args);
FTestActionResult hg03_dismissed_jailed_and_bridge(struct FTestActionArgs* const args);
FTestActionResult hg04_probe_and_drop_on_bridge(struct FTestActionArgs* const args);
FTestActionResult hg05_check_bridge_drop(struct FTestActionArgs* const args);

void ftest_ai_seat_hand_guards_pre_start() { fe_computer_players = 1; }

TbBool ftest_ai_seat_hand_guards_init()
{
    s_failures = 0;
    ftest_append_action(hg01_setup, 0, NULL);
    ftest_append_action(hg02_guards_then_for_real, 2, NULL);
    ftest_append_action(hg03_dismissed_jailed_and_bridge, 1, NULL);
    ftest_append_action(hg04_probe_and_drop_on_bridge, 1, NULL);
    ftest_append_action(hg05_check_bridge_drop, 1, NULL);
    return true;
}

FTestActionResult hg01_setup(struct FTestActionArgs* const args)
{
    P = -1;
    for (PlayerNumber p = 0; p < PLAYERS_COUNT; p++)
        if (p != my_player_number && p != PLAYER_GOOD && p != PLAYER_NEUTRAL && player_exists(get_player(p)) && !thing_is_invalid(find_players_dungeon_heart(p))) { P = p; break; }
    if (P < 0 || (U = net_add_external_seat(P)) < 1) { FTEST_FAIL_TEST("no seat"); return FTRs_Go_To_Next_Action; }
    const struct Thing* heart = find_players_dungeon_heart(P);
    hsx = subtile_slab(heart->mappos.x.stl.num); hsy = subtile_slab(heart->mappos.y.stl.num);
    if ((hsx - 6 < 1) || (hsx + 14 >= kfx_sim_state.map_tiles_x - 1) || (Y(8 + BRIDGE_LEN) < 1)) {
        FTEST_FAIL_TEST("the heart (%d,%d) is too near the edge of the %dx%d map for the layout", (int)hsx, (int)hsy, (int)kfx_sim_state.map_tiles_x, (int)kfx_sim_state.map_tiles_y); return FTRs_Go_To_Next_Action;
    }
    // A claimed strip north of the heart: prison, portal and library side by side, two rows of floor below them, then
    // lava for a bridge to cross from the floor at (hsx+1, hsy+7).
    fill(hsx - 6, Y(3), hsx + 14, Y(7), SlbT_CLAIMED, P);
    set_room_available(P, RoK_PRISON, 1, 1); set_room_available(P, RoK_ENTRANCE, 1, 1);
    set_room_available(P, RoK_LIBRARY, 1, 1); set_room_available(P, RoK_BRIDGE, 1, 1); set_room_available(P, RoK_TORTURE, 1, 1);
    fill(hsx - 6, Y(3), hsx - 4, Y(5), SlbT_PRISON, P);
    fill(hsx - 2, Y(3), hsx, Y(5), SlbT_ENTRANCE, P);
    fill(hsx + 2, Y(3), hsx + 4, Y(5), SlbT_LIBRARY, P);
    fill(hsx + 10, Y(3), hsx + 12, Y(5), SlbT_TORTURE, P);
    fill(hsx - 6, Y(8), hsx + 10, Y(8 + BRIDGE_LEN - 1), SlbT_LAVA, kfx_config_state.neutral_player_num);
    ftest_util_reveal_map(P);
    get_players_dungeon(get_player(P))->offmap_money_owned += 100000;
    get_players_dungeon(get_player(P))->total_money_owned += 100000;
    const struct Room* prison = slab_room_get(hsx - 5, Y(4));
    const struct Room* portal = slab_room_get(hsx - 1, Y(4));
    struct Room* library = slab_room_get(hsx + 3, Y(4));
    const struct Room* torture = slab_room_get(hsx + 11, Y(4));
    if (room_is_invalid(prison) || room_is_invalid(portal) || room_is_invalid(library) || room_is_invalid(torture)) { FTEST_FAIL_TEST("rooms not built"); return FTRs_Go_To_Next_Action; }
    s_prison = prison->index; s_portal = portal->index; s_library = library->index; s_torture = torture->index;
    // Two spellbooks on the library's shelves (two spells the seat does not have yet).
    s_books = 0;
    static const PowerKind spells[] = { PwrK_LIGHTNING, PwrK_SPEEDCRTR, PwrK_SIGHT, PwrK_CALL2ARMS };
    for (size_t i = 0; (i < sizeof(spells) / sizeof(spells[0])) && (s_books < 2); i++) {
        if (is_power_available(P, spells[i])) continue;
        const struct Thing* book = create_spell_in_library(library, get_power_model_stats(spells[i])->artifact_model,
            library->central_stl_x, library->central_stl_y);
        if (!thing_is_invalid(book)) s_books++;
    }
    // Creatures on the floor rows.
    struct Thing* orc = make_creature("ORC", hsx + 6, Y(6), 0);
    struct Thing* imp = make_creature("IMP", hsx + 7, Y(6), 0);
    struct Thing* fly = make_creature("FLY", hsx + 8, Y(6), 0);
    struct Thing* jail = make_creature("ORC", hsx + 9, Y(6), 0);
    struct Thing* rack = make_creature("ORC", hsx + 10, Y(6), 0);
    struct Thing* mistress = make_creature("DARK_MISTRESS", hsx + 11, Y(6), 0);
    if (!orc || !imp || !fly || !jail || !rack || !mistress || (s_books < 2)) { FTEST_FAIL_TEST("setup things (books %d)", (int)s_books); return FTRs_Go_To_Next_Action; }
    s_orc = orc->index; s_imp = imp->index; s_fly = fly->index; s_jail = jail->index; s_fly_born = fly->creation_turn;
    s_rack = rack->index; s_mistress = mistress->index;
    static const char* const kinds[GROUP_MAX] = { "DEMONSPAWN", "DEMONSPAWN", "DEMONSPAWN", "DRAGON", "DRAGON", "BILE_DEMON", "FLY", "TROLL" };
    s_group_n = kfx_config_state.conf.rules[P].gameplay.max_things_in_hand;
    if (s_group_n > GROUP_MAX) s_group_n = GROUP_MAX;
    for (int64_t g = 0; g < s_group_n; g++) {
        struct Thing* t = make_creature(kinds[g], hsx - 6 + g, Y(7), 5); // level 6
        if (!t) { FTEST_FAIL_TEST("no %s", kinds[g]); return FTRs_Go_To_Next_Action; }
        s_group[g] = t->index;
    }
    for (int i = 0; i < BRIDGE_LEN; i++) s_slab_ready_turn[i] = -1;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult hg02_guards_then_for_real(struct FTestActionArgs* const args)
{
    const MapSubtlCoord pcx = slab_subtile_center(hsx - 1), pcy = slab_subtile_center(Y(4)); // portal centre
    const MapSubtlCoord pbx = slab_subtile(hsx - 2, 0), pby = slab_subtile(Y(3), 2);       // a portal corner subtile
    const MapSubtlCoord jx = slab_subtile_center(hsx - 5), jy = slab_subtile_center(Y(4));  // prison centre
    const MapSubtlCoord fx = slab_subtile_center(hsx + 9), fy = slab_subtile_center(Y(7));  // plain floor
    // Prison.
    expect("an own creature dropped in the prison would be jailed", at(carry(s_orc), jx, jy), "WOULD_IMPRISON");
    expect("to_room the prison too", into(carry(s_orc), s_prison), "WOULD_IMPRISON");
    struct ExtSeatVerb v = at(carry(s_orc), jx, jy); v.imprison = true;
    expect("imprison: true accepts it", v, NULL);
    v = at(carry(s_orc), fx, fy); v.imprison = true;
    expect("imprison off a prison", v, "NOT_A_PRISON");
    expect("plain floor needs no flag", at(carry(s_orc), fx, fy), NULL);
    // Torture chamber.
    const MapSubtlCoord tx = slab_subtile_center(hsx + 11), ty = slab_subtile_center(Y(4));
    expect("an own creature dropped in the torture chamber would go on the rack", at(carry(s_orc), tx, ty), "WOULD_TORTURE");
    expect("to_room the torture chamber too", into(carry(s_orc), s_torture), "WOULD_TORTURE");
    v = into(carry(s_orc), s_torture); v.torture = true;
    expect("torture: true accepts it", v, NULL);
    v = at(carry(s_orc), jx, jy); v.torture = true;
    expect("torture off a torture chamber (in the prison)", v, "NOT_A_TORTURE_CHAMBER");
    expect("a dark mistress's kinky torture is her own choice", into(carry(s_mistress), s_torture), NULL);
    expect("an imp is not tortured", at(carry(s_imp), tx, ty), NULL);
    // Portal.
    expect("an own creature dropped on the portal centre would be dismissed", at(carry(s_orc), pcx, pcy), "WOULD_DISMISS");
    expect("on a portal corner subtile too", at(carry(s_orc), pbx, pby), "WOULD_DISMISS");
    expect("to_room the portal too", into(carry(s_orc), s_portal), "WOULD_DISMISS");
    v = at(carry(s_orc), pcx, pcy); v.dismiss = true;
    expect("dismiss: true accepts it", v, NULL);
    v = at(carry(s_orc), fx, fy); v.dismiss = true;
    expect("dismiss off a portal", v, "NOT_A_PORTAL");
    expect("an imp on the portal is not dismissed", at(carry(s_imp), pcx, pcy), NULL);
    // Library.
    expect("selling the only library with books on its shelves", sell(hsx + 2, Y(3), hsx + 4, Y(5)), "WOULD_LOSE_SPELLS");
    expect("one of its slabs holding the books", sell(hsx + 3, Y(4), hsx + 3, Y(4)), "WOULD_LOSE_SPELLS");
    v = sell(hsx + 2, Y(3), hsx + 4, Y(5)); v.lose_spells = true;
    expect("lose_spells: true accepts it", v, NULL);
    expect("a library slab without books", sell(hsx + 2, Y(3), hsx + 2, Y(3)), NULL);
    expect("plain floor", sell(hsx + 9, Y(6), hsx + 9, Y(6)), NULL);
    // For real: the fly leaves through the portal from a corner subtile, the orc goes to prison, the bridge is built.
    v = at(carry(s_fly), pbx, pby); v.dismiss = true;
    CHECK_TRUE("the fly's dismissal is accepted", submit(v, &s_ids[0]) == NULL);
    v = into(carry(s_jail), s_prison); v.imprison = true;
    CHECK_TRUE("the orc's imprisonment is accepted", submit(v, &s_ids[1]) == NULL);
    v = into(carry(s_rack), s_torture); v.torture = true;
    CHECK_TRUE("the other orc's torture is accepted", submit(v, NULL) == NULL);
    struct ExtSeatVerb b; memset(&b, 0, sizeof(b));
    b.kind = ESV_BuildRoom; snprintf(b.name, sizeof(b.name), "BRIDGE"); b.has_rect = true;
    b.slab_x0 = hsx + 1; b.slab_y0 = Y(8); b.slab_x1 = hsx + 1; b.slab_y1 = Y(8 + BRIDGE_LEN - 1);
    const char* e = submit(b, &s_ids[2]);
    CHECK_TRUE("the bridge is accepted", e == NULL);
    if (e) FTESTLOG("bridge refused: %s", e);
    // An order is checked against the map when it is sent: a drop on the bridge sent with its build_room finds lava
    // that is not the seat's yet (the engine lays a slab a turn) -- the transient CANNOT_DROP_HERE seen on keeporig:5.
    expect("a drop on a bridge slab that is not built yet", at(carry(s_orc), slab_subtile_center(hsx + 1), slab_subtile_center(Y(8))), "CANNOT_DROP_HERE");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult hg03_dismissed_jailed_and_bridge(struct FTestActionArgs* const args)
{
    // While the bridge goes up (one slab per turn), probe each slab's centre as soon as it is the seat's.
    int built = 0;
    for (int i = 0; i < BRIDGE_LEN; i++) {
        if (!bridge_slab_built(i)) continue;
        built++;
        if (s_slab_ready_turn[i] < 0) {
            s_slab_ready_turn[i] = (int64_t)get_gameturn();
            s_probe_failures += probe_subtile(slab_subtile_center(hsx + 1), slab_subtile_center(Y(8 + i)));
        }
    }
    const struct Thing* fly = thing_get(s_fly);
    const TbBool fly_gone = !thing_is_creature(fly) || (fly->creation_turn != s_fly_born) || (fly->owner != P);
    const TbBool jailed = creature_is_kept_in_prison(thing_get(s_jail));
    const TbBool racked = creature_is_being_tortured(thing_get(s_rack));
    const TbBool waited = (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + 600);
    if (!(fly_gone && jailed && racked && built == BRIDGE_LEN) && !waited) return FTRs_Repeat_Current_Action;
    FTESTLOG("fly gone %d, orc jailed %d, orc on the rack %d, bridge slabs %d", (int)fly_gone, (int)jailed, (int)racked, built);
    CHECK_TRUE("the fly dropped on a portal corner left the dungeon", fly_gone);
    CHECK_TRUE("the orc dropped with imprison: true is a prisoner of its own keeper", jailed);
    CHECK_TRUE("the orc dropped with torture: true is on its own keeper's rack", racked);
    CHECK_TRUE("the six-slab bridge was built", built == BRIDGE_LEN);
    // A second library with shelf space: the first one's books now have somewhere to go.
    fill(hsx + 6, Y(3), hsx + 8, Y(5), SlbT_LIBRARY, P);
    const struct Room* lib2 = slab_room_get(hsx + 7, Y(4));
    const int64_t spare = room_is_invalid(lib2) ? 0 : lib2->total_capacity - lib2->used_capacity;
    FTESTLOG("second library: capacity %d used %d, books to move %d", room_is_invalid(lib2) ? 0 : (int)lib2->total_capacity,
        room_is_invalid(lib2) ? 0 : (int)lib2->used_capacity, (int)s_books);
    expect(spare >= s_books ? "with shelf space elsewhere the library sells" : "still no shelf space elsewhere",
        sell(hsx + 2, Y(3), hsx + 4, Y(5)), spare >= s_books ? NULL : "WOULD_LOSE_SPELLS");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult hg04_probe_and_drop_on_bridge(struct FTestActionArgs* const args)
{
    // Every subtile of every bridge slab, for every creature of the group (big ones: demon spawn, dragons, bile demon,
    // and a flyer), standing on the floor beside the lava.
    int64_t bad = 0;
    for (int i = 0; i < BRIDGE_LEN; i++)
        for (int k = 0; k < 9; k++)
            bad += probe_subtile(slab_subtile(hsx + 1, k % 3), slab_subtile(Y(8 + i), k / 3));
    FTESTLOG("bridge probe: %d refusal(s) over %d subtiles x %d creatures; %d while it was being built",
        (int)bad, BRIDGE_LEN * 9, (int)s_group_n, (int)s_probe_failures);
    CHECK_TRUE("every creature may be dropped on every subtile of a finished bridge", bad == 0);
    CHECK_TRUE("and on each slab's centre as soon as that slab was built", s_probe_failures == 0);
    // The agent's order: the whole group in one pick_up_and_drop on the centre of the bridge's first slab.
    struct ExtSeatVerb v; memset(&v, 0, sizeof(v));
    v.kind = ESV_PickUpAndDrop; v.thing_count = s_group_n;
    for (int64_t g = 0; g < s_group_n; g++) v.thing_ids[g] = s_group[g];
    v.has_pos = true; v.stl_x = slab_subtile_center(hsx + 1); v.stl_y = slab_subtile_center(Y(8));
    const char* e = submit(v, &s_ids[3]);
    CHECK_TRUE("the group drop on the bridge is accepted", e == NULL);
    if (e) FTESTLOG("group drop refused: %s", e);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult hg05_check_bridge_drop(struct FTestActionArgs* const args)
{
    const struct ExtSeatResult* r = result_for(s_ids[3]);
    const TbBool waited = (args->intended_start_at_game_turn > 0) && ((int64_t)get_gameturn() > args->intended_start_at_game_turn + 300);
    if (!(extseat_idle(U) && r != NULL) && !waited) return FTRs_Repeat_Current_Action;
    if (r != NULL && r->rejected) SOFT_FAIL("the group drop was refused when it ran: %s", r->error);
    int64_t landed = 0;
    for (int64_t g = 0; g < s_group_n; g++) {
        const struct Thing* t = thing_get(s_group[g]);
        if (!thing_is_creature(t) || thing_is_picked_up(t)) continue;
        const MapSlabCoord sx = subtile_slab(t->mappos.x.stl.num), sy = subtile_slab(t->mappos.y.stl.num);
        if ((sx >= hsx) && (sx <= hsx + 2) && (sy <= Y(7)) && (sy >= Y(8 + BRIDGE_LEN))) landed++;
    }
    FTESTLOG("group on the bridge: %d of %d", (int)landed, (int)s_group_n);
    CHECK_TRUE("the whole group landed on the bridge", landed == s_group_n);
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " hand guard check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: drops that jail, torture, dismiss or lose spells are refused unless meant, and do what they say when meant");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
