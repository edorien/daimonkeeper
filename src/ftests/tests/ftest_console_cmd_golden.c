// Refactor pass 5, S03: golden hashes of what the console commands do (S02's coverage: console_cmd.c 5%, 12 of its
// 127 functions reached).
//
// keeporig level 11 is saved once; each case loads that save, turns cheat mode on (a load turns it off), runs one or more console command
// lines as player 0 (cmd_exec(), what typing "!<line>" in chat does) and hashes what changed in the game state
// (ftest_golden.h) together with each line's result: what cmd_exec() returned and the replies it added to the
// message list. Every command in console_cmd.c's table runs with typical arguments and the wrong ones it checks for.
//
// A case is "line ; line ; ..."; a leading '-' runs it with cheat mode off. Before each case the cursor (player 0's
// packet position, which the commands that create or look at things use) is put on the subtile of player 0's
// creature with the lowest thing index; in a line, $C0 is that creature's thing index, $HERO the heroes' creature's
// with the lowest index, $HEART player 0's heart's, $ROOM the index of player 0's room with the lowest index.
// "thing.get $C0" selects the creature, as the commands acting on "the selected creature" need.
//
// What a load doesn't put back (the draw rate, the frame-time display, the quit flags, the volume settings and
// their file, the timer flag, the font switch, the cheat menus) is put back after each case; the commands that
// only toggle display state run twice in their case. The version a "ver" reply shows is replaced by "<version>",
// so a new build number changes no hash.
//
// To print the hashes (only on a commit meant to change what a command does): KFX_FTEST_GOLDEN_PRINT=1 in the
// tree StageFtestData.cmake staged (ftest_golden.h); the lines are logged with the prefix "GOLDEN:", each with the
// start of its result.
#include "ftest_console_cmd_golden.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdio.h>
#include <string.h>

#include "../ftest.h"
#include "../ftest_golden.h"

#include "bflib_basics.h"
#include "bflib_datetm.h"
#include "bflib_sndlib.h"
#include "bflib_sprfnt.h"
#include "bflib_video.h"
#include "config_keeperfx.h"
#include "config_settings.h"
#include "console_cmd.h"
#include "dungeon_data.h"
#include "game_merge.h"
#include "frontend.h"
#include "kfx_sim_state.h"
#include "packet_data.h"
#include "player_data.h"
#include "room_data.h"
#include "thing_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GOLDEN_SAVE_SLOT 8

static const char *const golden_cases[] = {
    // the dispatcher
    "", "notacommand", "STATS", "-stats", "-reveal", "-notacommand", "-thing.get $C0", "-room.get $ROOM",
    // information, timing, the game's flow
    "stats", "fps", "fps 30", "fps.draw 60", "ft", "frametime.max", "ft.max ; ft.max", "netstats ; netstats",
    "quit", "time", "time 1200 20", "timer.toggle", "timer.switch", "turn", "pause", "pause ; pause", "step",
    "game.save 9 golden ; game.load 9", "game.save -1", "game.load -1", "game.load 9999",
    "cls", "ver", "volume", "volume.sound", "volume.sound abc", "volume.sound 50", "volume.sfx 40",
    "volume.music 30", "volume.soundtrack abc",
    // the computer players
    "compuchat", "compuchat scarce", "compuchat 2", "compuchat none",
    "comp.procs 1 ; cheat.menu 0", "comp.events 1 ; cheat.menu 0", "comp.checks 1 ; cheat.menu 0",
    "comp.procs", "comp.events 9", "comp.checks -1",
    "comp.kill 1", "comp.kill 2", "comp.kill", "comp.kill 99", "comp.kill 9",
    "comp.me 1", "comp.me", "spectate", "spectate 1",
    // the map
    "reveal", "reveal 10", "conceal", "conceal 10",
    "slab.place lava", "slab.place claimed", "slab.place rock", "slab.place gold 1", "slab.place temple",
    "slab.place 10", "slab.place 99", "slab.place notaslab", "slab.place", "slab.place earth",
    "slab.place reinforced", "slab.place fortified",
    "slab.health", "slab.health 100",
    "look treasure", "look troll", "look 1", "look notalocation", "look",
    "zoomto.slabcoord 10 10", "zoomto.slabcoord 10", "zoomto.slabcoord", "zoomto.slabcoord 999 999",
    "zoomto.subtilecoord 40 40", "zoomto.subtilecoord 40", "zoomto.subtilecoord 999 1",
    "actionpoint.pos 1", "actionpoint.pos 99", "actionpoint.pos", "actionpoint.zoomto 1", "zoomto.actionpoint 99",
    "actionpoint.reset 1", "actionpoint.reset 1 0",
    "herogate.zoomto 1", "zoomto.herogate 9", "herogate.zoomto",
    "mapwho.info", "mapwho.info 40 40", "mapwho.info 40", "mapwho.info 999 999",
    "cursor.pos",
    // players
    "player.score", "player.score 1", "player.score 99",
    "player.flag 0 3", "player.flag 0 3 9 ; player.flag 0 3", "player.flag 0 99", "player.flag 99",
    "player.heart.health", "player.heart.health 0 1000", "player.heart.health 1", "player.heart.health 0 0",
    "player.heart.health 99",
    "player.gold.add 0 1000", "player.addgold 1 500", "player.gold.add 0", "player.gold.add 99 1",
    "player.colour 0 blue", "player.color 1 green", "player.colour 0", "player.colour", "player.colour 0 notacolour",
    "possession.lock", "possession.unlock",
    "string.show 5", "string.show", "quick.show 5", "quick.show -1",
    // what players can build and use
    "give.trap boulder 2", "trap.give gas", "give.trap 99", "give.trap",
    "give.door wooden", "door.give braced 3", "give.door 99", "give.door",
    "room.available temple", "room.available all", "room.available temple 0", "room.available hatchery",
    "room.available guard 1 1", "room.available",
    "power.give all", "spell.give POWER_LIGHTNING", "power.give", "power.give 999",
    "creature.available troll", "creature.available troll 0", "creature.available notacreature",
    "magic.instance imp 1 fireball", "magic.instance imp 1 2", "magic.instance", "magic.instance imp",
    "magic.instance imp 1", "magic.instance notacreature 1 fireball", "magic.instance imp 12 fireball",
    "magic.instance imp 1 0", "magic.instance imp 1 9999",
    // the creature pool
    "map.pool", "creature.pool troll", "map.pool troll 7", "map.pool troll -1", "map.pool notacreature",
    "creature.pool.add troll 3", "creature.pool.sub troll 1", "creature.pool.remove troll 1",
    "creature.pool.add troll", "creature.pool.add 0 3", "creature.pool.add any 5", "creature.pool.add evil 5",
    "creature.pool.sub 500 1",
    // creating things
    "gold.create 500", "create.gold", "gold.create abc",
    "object.create barrel", "create.object barrel 1", "object.create 5", "object.create", "object.create notanobject",
    "creature.create troll", "create.creature troll 5 2", "creature.create troll 1 1 1",
    "creature.create evil", "creature.create good", "creature.create any", "creature.create 999",
    "creature.create notacreature",
    "creature.create",
    "thing.create object barrel", "create.thing corpse troll", "thing.create creature imp",
    "thing.create trap boulder", "thing.create effect EFFECT_EXPLOSION_1", "thing.create shot SHOT_FIREBALL",
    "thing.create 1 5", "thing.create nonsense x", "thing.create object", "thing.create",
    // things and the selected one
    "thing.get", "thing.get $C0", "thing.get 0", "thing.get $HEART", "thing.get $HERO",
    "thing.info", "thing.get $C0 ; thing.info", "thing.get $HEART ; thing.info",
    "thing.show_id ; thing.show_id 0",
    "thing.get $C0 ; thing.health", "thing.get $C0 ; thing.health 5", "thing.health",
    "thing.get $C0 ; thing.move 40 40", "thing.get $C0 ; thing.move 40 40 3", "thing.get $C0 ; thing.move",
    "thing.get $C0 ; thing.move 999 999", "thing.move",
    "thing.destroy", "thing.get 0 ; thing.destroy", "thing.get $C0 ; thing.destroy", "thing.get $HEART ; thing.destroy",
    "creature.addhealth 10", "thing.get $C0 ; creature.addhealth 10", "thing.get $C0 ; creature.health.sub 10",
    "thing.get $C0 ; creature.subhealth abc", "creature.health.add",
    "thing.get $C0 ; creature.level 5", "creature.level", "creature.level 5",
    "thing.get $C0 ; creature.freeze", "thing.get $C0 ; creature.slow", "creature.freeze",
    "thing.get $C0 ; creature.chicken", "creature.chicken",
    "thing.get $C0 ; creature.instance.set 2", "creature.instance.set", "creature.instance.set 2",
    "thing.get $C0 ; creature.state.set 1", "thing.get $C0 ; creature.state.set 0", "creature.state.set",
    "thing.get $C0 ; creature.job.set 1", "thing.get $C0 ; creature.job.set 3", "creature.job.set",
    "thing.get $C0 ; creature.job.set 70",
    "thing.get $C0 ; creature.attackheart 1", "thing.get $C0 ; creature.attackheart 99", "creature.attackheart",
    "creature.attackheart 1",
    "digger.sendto", "digger.sendto 1", "thing.get $C0 ; digger.sendto 0", "digger.sendto 99",
    // rooms
    "room.get", "room.get $ROOM", "room.get 0", "room.get 511", "room.get $ROOM ; room.health", "room.get $ROOM ; room.health 10",
    "room.health",
    // the rules
    "bug.toggle", "bug.toggle 3", "bug.toggle RESURRECT_FOREVER", "bug.toggle CLAIM_ROOM_ALL_THINGS",
    "bug.toggle ALWAYS_TUNNEL_TO_RED", "bug.toggle 19", "bug.toggle 0", "bug.toggle notabug",
    // sound, display, scripts, the rest
    "music.set 2", "music.set", "sound.test 1", "sound.test", "speech.test 1", "speech.test",
    "toggle.tooltip.land.coord ; toggle.tooltip.land.coord", "toggle.lights",
    "lua PLAYER0.MONEY = 7", "lua not valid lua(",
    "cheat.menu 1 ; cheat.menu 0", "cheat.menu 2 ; cheat.menu 0", "cheat.menu instance ; cheat.menu none",
    "cheat.menu secondary ; cheat.menu 0", "cheat.menu 9", "cheat.menu",
    "dbc ; dbc", "resync",
};

#define GOLDEN_COUNT(a) ((int64_t)(sizeof(a) / sizeof((a)[0])))

static struct FTestGolden s_golden;
static int64_t s_next;

static const struct FTestGoldenExpected golden_expected[] = {
#include "ftest_console_cmd_golden.inc"
    {NULL, 0}
};

/** What a load doesn't put back. */
struct GoldenLocals {
    int64_t fps_limit_main, fps_limit_secondary, debug_display_frametime;
    unsigned char quit_game, exit_keeper;
    uint64_t game_flags2;
    TbBool dbc_initialized;
    struct GameSettings settings;
};

static void golden_locals_take(struct GoldenLocals *l)
{
    l->fps_limit_main = fps_limit_main;
    l->fps_limit_secondary = fps_limit_secondary;
    l->debug_display_frametime = debug_display_frametime;
    l->quit_game = quit_game;
    l->exit_keeper = exit_keeper;
    l->game_flags2 = game_flags2;
    l->dbc_initialized = dbc_initialized;
    l->settings = settings;
}

static void golden_locals_restore(const struct GoldenLocals *l)
{
    fps_limit_main = l->fps_limit_main;
    fps_limit_secondary = l->fps_limit_secondary;
    debug_display_frametime = l->debug_display_frametime;
    quit_game = l->quit_game;
    exit_keeper = l->exit_keeper;
    game_flags2 = l->game_flags2;
    dbc_initialized = l->dbc_initialized;
    if (memcmp(&settings, &l->settings, sizeof(settings)) != 0)
    {
        settings = l->settings;
        save_settings();
        SetSoundMasterVolume(settings.sound_volume);
        set_music_volume(settings.music_volume);
    }
    close_main_cheat_menu();
    close_creature_cheat_menu();
    close_instance_cheat_menu();
    close_secondary_cheat_menu();
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

/** The line with $C0, $HERO, $HEART and $ROOM replaced. */
static void golden_expand(const char *line, char *out, size_t n)
{
    static const char *const names[] = {"$C0", "$HERO", "$HEART", "$ROOM"};
    int64_t values[4];
    values[0] = golden_first_creature(PLAYER0);
    values[1] = golden_first_creature(PLAYER_GOOD);
    values[2] = get_player_soul_container(PLAYER0)->index;
    values[3] = golden_first_room(PLAYER0);
    size_t o = 0;
    while ((*line != '\0') && (o + 1 < n))
    {
        int64_t k;
        for (k = 0; k < GOLDEN_COUNT(names); k++)
        {
            size_t len = strlen(names[k]);
            if (strncmp(line, names[k], len) == 0)
            {
                o += snprintf(out + o, n - o, "%" PRId64, values[k]);
                line += len;
                break;
            }
        }
        if (k == GOLDEN_COUNT(names))
            out[o++] = *line++;
    }
    out[(o < n) ? o : n - 1] = '\0';
}

/** Puts the cursor (player 0's packet position) on player 0's first creature's subtile. */
static void golden_place_cursor(void)
{
    NetUserId user = get_player(PLAYER0)->user_id;
    if (user < 0)
        user = get_local_user();
    struct Packet *pckt = get_packet(user);
    struct Thing *thing = thing_get(golden_first_creature(PLAYER0));
    pckt->pos_x = subtile_coord_center(thing->mappos.x.stl.num);
    pckt->pos_y = subtile_coord_center(thing->mappos.y.stl.num);
}

/** Runs one line; appends "ret=<0|1>" and the replies it added (oldest first) to result. */
static void golden_run_line(const char *line, char *result, size_t n)
{
    struct GuiMessage before[GUI_MESSAGES_COUNT];
    memcpy(before, kfx_sim_state.messages, sizeof(before));
    const int64_t count_before = kfx_sim_state.active_messages_count;
    char buf[512];
    golden_expand(line, buf, sizeof(buf));
    const TbBool ret = cmd_exec(PLAYER0, buf);
    // a "ver" reply holds the build number
    for (int64_t i = 0; i < GUI_MESSAGES_COUNT; i++)
    {
        char *ver = strstr(kfx_sim_state.messages[i].text, "(ver ");
        if (ver != NULL)
            snprintf(ver, sizeof(kfx_sim_state.messages[i].text) - (ver - kfx_sim_state.messages[i].text),
                "(ver <version>)");
    }
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
    size_t o = strlen(result);
    o += snprintf(result + o, (o < n) ? n - o : 0, "%sret=%d", (o > 0) ? " ; " : "", ret ? 1 : 0);
    if (added > kfx_sim_state.active_messages_count)
        added = kfx_sim_state.active_messages_count;
    for (int64_t i = added - 1; i >= 0; i--)
        o += snprintf(result + o, (o < n) ? n - o : 0, " \"%s\"", kfx_sim_state.messages[i].text);
}

/** Runs a case: its lines, separated by " ; ". */
static void golden_run_case(const char *text, char *result, size_t n)
{
    result[0] = '\0';
    kfx_sim_state.easter_eggs_enabled = (text[0] != '-');
    if (text[0] == '-')
        text++;
    golden_place_cursor();
    char line[512];
    for (;;)
    {
        const char *end = strstr(text, " ; ");
        size_t len = (end != NULL) ? (size_t)(end - text) : strlen(text);
        snprintf(line, sizeof(line), "%.*s", (int)len, text);
        golden_run_line(line, result, n);
        if (end == NULL)
            break;
        text = end + 3;
    }
}

FTestActionResult ftest_console_cmd_golden_action001(struct FTestActionArgs* const args)
{
    if (!s_golden.saved)
    {
        if (!ftest_golden_begin(&s_golden, GOLDEN_SAVE_SLOT, golden_expected))
            return FTRs_Go_To_Next_Action;
        FTESTLOG("%" PRId64 " console command cases", GOLDEN_COUNT(golden_cases));
    }
    // a batch of cases per turn (each loads the save, so the turn's own processing doesn't matter)
    for (int64_t batch = 0; (batch < 8) && (s_next < GOLDEN_COUNT(golden_cases)); batch++, s_next++)
    {
        const char *text = golden_cases[s_next];
        char name[600];
        snprintf(name, sizeof(name), "console:%s", text);
        if (!ftest_golden_selected(name))
            continue;
        if (!ftest_golden_load(&s_golden))
        {
            FTEST_FAIL_TEST("load_game failed");
            return FTRs_Go_To_Next_Action;
        }
        struct GoldenLocals locals;
        golden_locals_take(&locals);
        char result[2048];
        golden_run_case(text, result, sizeof(result));
        golden_locals_restore(&locals);
        ftest_golden_check_result(&s_golden, name, result);
    }
    if (s_next < GOLDEN_COUNT(golden_cases))
        return FTRs_Repeat_Current_Action;
    ftest_golden_finish(&s_golden, "console command");
    return FTRs_Go_To_Next_Action;
}

TbBool ftest_console_cmd_golden_init()
{
    memset(&s_golden, 0, sizeof(s_golden));
    s_next = 0;
    ftest_append_action(ftest_console_cmd_golden_action001, 30, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
