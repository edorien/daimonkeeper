// Security pass S06 (docs/security-pass/README.md): which corruptions of a simulation state does the validator
// (state_validate.h) have to catch? The computer keepers of original level 60 play on their own to a turn in the
// middle of the game; then, for each of a few hundred mutants, a child process (fork) corrupts one value of the live
// state -- a word in a thing, a creature's control block, a room, a dungeon, a player, a light, a map block, a slab, or
// anywhere in the state where something is stored -- asks the validator whether it would let the state in, and plays
// on for some turns to see whether the game survives.
//
//   accepted + survived   the validator and the game both cope with it
//   refused  + survived   an over-strict check, or a value the game happens to cope with (not a problem)
//   refused  + crashed    what the validator is for
//   ACCEPTED + CRASHED    a GAP: the corruption gets past the validator and takes the game down (or hangs it)
//
// A measurement more than a test: it is in the long-running list (-ftests state_corruption_fuzz -includelongtests)
// and fails only with KFX_FUZZ_STRICT=1 and a gap. Every mutant is logged ("FUZZ ...") and its fate ("FUZZRESULT ...")
// so a gap can be found: the offset is within the named region's element (map it to a field with `ptype /o struct X`
// in gdb). Environment: KFX_FUZZ_T (turn to corrupt at, 600), KFX_FUZZ_COUNT (mutants, 200), KFX_FUZZ_TURNS (turns each
// plays, 100), KFX_FUZZ_JOBS (children at once, 8), KFX_FUZZ_SEED, KFX_FUZZ_REGION=<name> to take them all from one.
#include "ftest_state_corruption_fuzz.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <execinfo.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../ftest.h"
#include "../ftest_util.h"

#include "config_keeperfx.h"
#include "ariadne_saved_state.h"
#include "kfx_config_state.h"
#include "kfx_game_state.h"
#include "game_state_validate.h"
#include "config_state_validate.h"
#include "kfx_net_state.h"
#include "creature_control.h"
#include "frontend.h"
#include "dungeon_data.h"
#include "kfx_sim_state.h"
#include "light_registry.h"
#include "player_data.h"
#include "player_utils.h"
#include "room_data.h"
#include "state_validate.h"
#include "thing_list.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WIN32

static int64_t env_int(const char *name, int64_t def)
{
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? (int64_t)strtoll(v, NULL, 0) : def;
}

struct Region
{
    const char *name;
    size_t base;      /**< offset of the first element in kfx_sim_state */
    size_t stride;
    size_t count;
    TbBool (*in_use)(size_t idx);
};

static TbBool thing_in_use(size_t i) { return (kfx_sim_state.things_data[i].alloc_flags & TAlF_Exists) != 0; }
static TbBool cctrl_in_use(size_t i) { return kfx_sim_state.cctrl_data[i].index != 0; }
static TbBool room_in_use(size_t i) { return kfx_sim_state.rooms[i].index != 0; }
static TbBool dungeon_in_use(size_t i) { return kfx_sim_state.dungeon[i].dnheart_idx != 0 || kfx_sim_state.dungeon[i].creatr_list_start != 0; }
static TbBool player_in_use(size_t i) { return (kfx_sim_state.players[i].allocflags & PlaF_Allocated) != 0; }
static TbBool light_in_use(size_t i) { return kfx_sim_state.light_registry.lights[i].index != 0; }
static TbBool block_in_use(size_t i) { return kfx_sim_state.map[i].mapwho != 0; }
static TbBool slab_in_use(size_t i) { return kfx_sim_state.slabmap[i].room_index != 0 || kfx_sim_state.slabmap[i].next_in_room != 0; }
static TbBool any_element(size_t i) { (void)i; return true; }

#define REGION(nm, field, pred) { nm, offsetof(struct KfxSimState, field), sizeof(((struct KfxSimState *)0)->field[0]), \
    sizeof(((struct KfxSimState *)0)->field) / sizeof(((struct KfxSimState *)0)->field[0]), pred }

static const struct Region regions[] = {
    REGION("things", things_data, thing_in_use),
    REGION("things", things_data, thing_in_use),
    REGION("things", things_data, thing_in_use),
    REGION("cctrl", cctrl_data, cctrl_in_use),
    REGION("cctrl", cctrl_data, cctrl_in_use),
    REGION("rooms", rooms, room_in_use),
    REGION("dungeon", dungeon, dungeon_in_use),
    REGION("players", players, player_in_use),
    REGION("user_states", user_states, any_element),
    REGION("map", map, block_in_use),
    REGION("slabmap", slabmap, slab_in_use),
    REGION("lights", light_registry.lights, light_in_use),
    REGION("thing_lists", thing_lists, any_element),
    REGION("events", event, any_element),
    REGION("battles", battles, any_element),
    REGION("columns", columns_data, any_element),
    REGION("computer_task", computer_task, any_element),
    REGION("action_points", action_points, any_element),
    { "anywhere", 0, 8, sizeof(struct KfxSimState) / 8, NULL },
    { "anywhere", 0, 8, sizeof(struct KfxSimState) / 8, NULL },
};

enum Blob { Blob_Sim = 0, Blob_Ariadne, Blob_Config, Blob_Net, Blob_Game };

static const char *const blob_names[] = { "sim", "ariadne", "config", "net", "game" };

struct Mutant
{
    int id;
    enum Blob blob;
    const struct Region *region;
    size_t index;
    size_t abs_offset;
    int width;
    uint64_t value;
};

static uint64_t rng_state;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static const uint64_t interesting[] = {
    ~0ULL, 0, 1, 2, 0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL, 0xFFFF, 0x10000, 12288, 12287, 255, 256, 1ULL << 20,
    1ULL << 31, 0x7FFFFFFF, 0x80000000ULL, 0xFFFFFFFFULL, 1024, 511, 1ULL << 40,
};

static TbBool word_is_nonzero(size_t abs, int width)
{
    const unsigned char *b = (const unsigned char *)&kfx_sim_state + abs;
    for (int i = 0; i < width; i++)
        if (b[i] != 0)
            return true;
    return false;
}

static const unsigned char *blob_base(enum Blob blob, size_t *size)
{
    switch (blob)
    {
    case Blob_Config:
        *size = KFX_CONFIG_STATE_SAVED_LEN;
        return (const unsigned char *)KFX_CONFIG_STATE_SAVED_PTR;
    case Blob_Net:
        *size = sizeof(kfx_net_state);
        return (const unsigned char *)&kfx_net_state;
    case Blob_Game:
        *size = sizeof(kfx_game_state);
        return (const unsigned char *)&kfx_game_state;
    default:
        *size = 0;
        return NULL;
    }
}

static unsigned char *s_ariadne_buffer = NULL;
static size_t s_ariadne_size = 0;

/** A mutant in one of the other blobs a save or a resync carries: a word that holds something, anywhere in it. */
static void make_blob_mutant(struct Mutant *m, enum Blob blob)
{
    m->blob = blob;
    size_t size = 0;
    const unsigned char *base = (blob == Blob_Ariadne) ? s_ariadne_buffer : blob_base(blob, &size);
    if (blob == Blob_Ariadne)
        size = s_ariadne_size;
    static const int widths[] = { 8, 8, 4, 2, 1 };
    m->width = widths[rng() % (sizeof(widths) / sizeof(widths[0]))];
    size_t abs = 0;
    for (int tries = 0; tries < 200000; tries++)
    {
        abs = (size_t)(rng() % (size / (size_t)m->width)) * (size_t)m->width;
        int nonzero = 0;
        for (int i = 0; i < m->width; i++)
            nonzero |= base[abs + (size_t)i] != 0;
        if (nonzero)
            break;
    }
    m->abs_offset = abs;
    m->value = interesting[rng() % (sizeof(interesting) / sizeof(interesting[0]))];
}

static void make_mutant(struct Mutant *m, int id, const char *only_region)
{
    memset(m, 0, sizeof(*m));
    m->id = id;
    {
        // a share of the mutants go to the other blobs (the navigation mesh, the configuration, ...)
        const char *only_blob = getenv("KFX_FUZZ_BLOB");
        const int pick = (only_blob != NULL) ? -1 : (int)(rng() % 10);
        enum Blob blob = Blob_Sim;
        if (only_blob != NULL)
        {
            for (int b = 0; b < 5; b++)
                if (strcmp(only_blob, blob_names[b]) == 0)
                    blob = (enum Blob)b;
        }
        else if (pick == 0) blob = Blob_Ariadne;
        else if (pick == 1) blob = Blob_Config;
        else if (pick == 2) blob = (rng() % 2) ? Blob_Net : Blob_Game;
        if (blob != Blob_Sim)
        {
            make_blob_mutant(m, blob);
            static const struct Region blob_region = { "blob", 0, 1, 0, NULL };
            m->region = &blob_region;
            return;
        }
    }
    const struct Region *r;
    do
    {
        r = &regions[rng() % (sizeof(regions) / sizeof(regions[0]))];
    } while (only_region != NULL && strcmp(r->name, only_region) != 0);
    m->region = r;
    static const int widths[] = { 8, 8, 8, 4, 2, 1, 1 };
    m->width = widths[rng() % (sizeof(widths) / sizeof(widths[0]))];
    // an element in use (a few hundred tries; the sparse regions are mostly empty), then a place within it
    size_t idx = rng() % r->count;
    if (r->in_use != NULL)
    {
        for (int tries = 0; tries < 20000 && !r->in_use(idx); tries++)
            idx = rng() % r->count;
    }
    m->index = idx;
    for (int tries = 0; tries < 64; tries++)
    {
        const size_t within = (size_t)(rng() % r->stride) & ~(size_t)(m->width - 1);
        const size_t abs = r->base + idx * r->stride + within;
        if (abs + (size_t)m->width > sizeof(struct KfxSimState))
            continue;
        m->abs_offset = abs;
        // inside an element, favour places that hold something: a zero is mostly padding or an unused field
        if (word_is_nonzero(abs, m->width) || tries == 63)
            break;
    }
    m->value = interesting[rng() % (sizeof(interesting) / sizeof(interesting[0]))];
}

/** The validator's verdict on the state with the mutation in it: 1 accepted, 0 refused, 2 none exists for this blob. */
static unsigned char apply_mutant(const struct Mutant *m)
{
    switch (m->blob)
    {
    case Blob_Sim:
    {
        unsigned char *b = (unsigned char *)&kfx_sim_state + m->abs_offset;
        memcpy(b, &m->value, (size_t)m->width); // little-endian: the low bytes
        return sim_state_validate(&kfx_sim_state, NULL, 0) ? 1 : 0;
    }
    case Blob_Ariadne:
    {
        memcpy(s_ariadne_buffer + m->abs_offset, &m->value, (size_t)m->width);
        const unsigned char verdict = ariadne_saved_state_check(s_ariadne_buffer, NULL, 0) ? 1 : 0;
        ariadne_saved_state_read(s_ariadne_buffer);
        return verdict;
    }
    default:
    {
        size_t size = 0;
        unsigned char *b = (unsigned char *)blob_base(m->blob, &size);
        memcpy(b + m->abs_offset, &m->value, (size_t)m->width);
        if (m->blob == Blob_Game)
            return game_state_validate(&kfx_game_state, NULL, 0) ? 1 : 0;
        if (m->blob == Blob_Config)
            return config_state_validate(&kfx_config_state, NULL, 0) ? 1 : 0;
        return 2;
    }
    }
}

/** A hung child: say where it was (addresses; resolve with addr2line -f -e daimonkeeper.debug) and leave with 99. */
static void hang_handler(int sig)
{
    (void)sig;
    void *frames[48];
    const int n = backtrace(frames, 48);
    static const char head[] = "FUZZHANG backtrace:\n";
    (void)!write(2, head, sizeof(head) - 1);
    backtrace_symbols_fd(frames, n, 2);
    _exit(99);
}

struct Running
{
    pid_t pid;
    int pipe_read;
    struct Mutant m;
};

static TbBool s_in_child = false;
static GameTurn s_child_stop_turn = 0;
static int s_phase = 0;
static int64_t s_t_corrupt, s_mutants, s_turns_each, s_jobs;
static int64_t s_survived_accepted, s_survived_refused, s_crashed_accepted, s_crashed_refused, s_hung_accepted, s_hung_refused;

static void describe(const struct Mutant *m, char *out, size_t size)
{
    snprintf(out, size, "m=%d blob=%s region=%s idx=%llu within=%llu abs=%llu width=%d value=0x%llx", m->id, blob_names[m->blob], m->region->name,
        (unsigned long long)m->index, (unsigned long long)(m->abs_offset - m->region->base - m->index * m->region->stride),
        (unsigned long long)m->abs_offset, m->width, (unsigned long long)m->value);
}

static void collect(struct Running *r)
{
    int status = 0;
    waitpid(r->pid, &status, 0);
    unsigned char verdict = 255;
    if (read(r->pipe_read, &verdict, 1) != 1)
        verdict = 255;
    close(r->pipe_read);
    const char *v = (verdict == 1) ? "accepted" : (verdict == 0) ? "refused" : (verdict == 2) ? "no-validator" : "unknown";
    char what[200];
    describe(&r->m, what, sizeof(what));
    if (WIFSIGNALED(status))
    {
        const int sig = WTERMSIG(status);
        const TbBool hang = (sig == SIGALRM);
        if (verdict == 1 || verdict == 2)
        {
            if (hang) s_hung_accepted++; else s_crashed_accepted++;
            FTESTLOG("FUZZGAP %s validator=%s outcome=%s(signal %d)", what, v, hang ? "HANG" : "CRASH", sig);
        }
        else
        {
            if (hang) s_hung_refused++; else s_crashed_refused++;
            FTESTLOG("FUZZRESULT %s validator=%s outcome=%s(signal %d)", what, v, hang ? "HANG" : "CRASH", sig);
        }
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 99)
    {
        // the child's alarm handler: it hung (the backtrace is in the stderr file)
        if (verdict == 1 || verdict == 2)
        {
            s_hung_accepted++;
            FTESTLOG("FUZZGAP %s validator=%s outcome=HANG(backtrace in stderr)", what, v);
        }
        else
        {
            s_hung_refused++;
            FTESTLOG("FUZZRESULT %s validator=%s outcome=HANG(backtrace in stderr)", what, v);
        }
    }
    else
    {
        if (verdict == 1 || verdict == 2) s_survived_accepted++; else s_survived_refused++;
        FTESTLOG("FUZZRESULT %s validator=%s outcome=survived(exit %d)", what, v, WEXITSTATUS(status));
    }
}

/** In the parent: fork the mutants in batches; each child returns from here to play on with its corruption. */
static TbBool run_mutants(void)
{
    const char *only = getenv("KFX_FUZZ_REGION");
    struct Running running[64];
    int64_t made = 0;
    while (made < s_mutants)
    {
        int batch = 0;
        while (batch < s_jobs && made < s_mutants && batch < 64)
        {
            struct Mutant m;
            make_mutant(&m, (int)made, only);
            if (getenv("KFX_FUZZ_ONLY") != NULL && (int)made != (int)env_int("KFX_FUZZ_ONLY", -1))
            {
                made++; // (generated, to keep the sequence, but not run)
                continue;
            }
            char what[200];
            describe(&m, what, sizeof(what));
            FTESTLOG("FUZZ %s", what);
            int fds[2];
            if (pipe(fds) != 0)
                return false;
            fflush(NULL);
            const pid_t pid = fork();
            if (pid < 0)
                return false;
            if (pid == 0)
            {
                // the child: corrupt, ask the validator, and play on
                close(fds[0]);
                signal(SIGALRM, hang_handler);
                alarm(60);
                const unsigned char accepted = apply_mutant(&m);
                (void)!write(fds[1], &accepted, 1);
                close(fds[1]);
                s_in_child = true;
                s_child_stop_turn = get_gameturn() + (GameTurn)s_turns_each;
                ftest_disable_state_validation();
                return true;
            }
            close(fds[1]);
            running[batch].pid = pid;
            running[batch].pipe_read = fds[0];
            running[batch].m = m;
            batch++;
            made++;
        }
        for (int i = 0; i < batch; i++)
            collect(&running[i]);
    }
    return true;
}

FTestActionResult ftest_state_corruption_fuzz_action001(struct FTestActionArgs* const args)
{
    const GameTurn t = get_gameturn();
    if (s_in_child)
    {
        if (t >= s_child_stop_turn)
        {
            fflush(NULL);
            _exit(0);
        }
        return FTRs_Repeat_Current_Action;
    }
    switch (s_phase)
    {
    case 0:
        s_t_corrupt = env_int("KFX_FUZZ_T", 600);
        s_mutants = env_int("KFX_FUZZ_COUNT", 200);
        s_turns_each = env_int("KFX_FUZZ_TURNS", 100);
        s_jobs = env_int("KFX_FUZZ_JOBS", 8);
        rng_state = (uint64_t)env_int("KFX_FUZZ_SEED", 12345) * 0x9E3779B97F4A7C15ULL + 1;
        if (!player_enter_spectator_mode(my_player_number))
            FTEST_FAIL_TEST("could not hand the local seat to the built-in AI");
        FTESTLOG("computer keepers play to turn %" PRId64 "; then %" PRId64 " mutants, %" PRId64 " turns each", s_t_corrupt, s_mutants, s_turns_each);
        s_phase = 1;
        return FTRs_Repeat_Current_Action;
    case 1:
        if ((int64_t)t < s_t_corrupt)
            return FTRs_Repeat_Current_Action;
        s_phase = 2;
        s_ariadne_size = ariadne_saved_state_size();
        s_ariadne_buffer = (unsigned char *)malloc(s_ariadne_size);
        if (s_ariadne_buffer == NULL)
        {
            FTEST_FAIL_TEST("no memory for the navigation mesh copy");
            return FTRs_Go_To_Next_Action;
        }
        ariadne_saved_state_write(s_ariadne_buffer);
        if (!run_mutants())
        {
            FTEST_FAIL_TEST("could not start the mutants");
            return FTRs_Go_To_Next_Action;
        }
        if (s_in_child)
            return FTRs_Repeat_Current_Action; // the child plays on
        break;
    default:
        break;
    }
    FTESTLOG("FUZZSUMMARY mutants=%" PRId64 ": accepted+survived %" PRId64 ", refused+survived %" PRId64
        ", refused+crashed %" PRId64 ", refused+hung %" PRId64 ", ACCEPTED+CRASHED %" PRId64 ", ACCEPTED+HUNG %" PRId64,
        s_mutants, s_survived_accepted, s_survived_refused, s_crashed_refused, s_hung_refused, s_crashed_accepted, s_hung_accepted);
    clear_flag(get_player(my_player_number)->allocflags, PlaF_CompCtrl);
    if (env_int("KFX_FUZZ_STRICT", 0) && (s_crashed_accepted + s_hung_accepted) > 0)
        FTEST_FAIL_TEST("%" PRId64 " corruption(s) got past the validator and took the game down", s_crashed_accepted + s_hung_accepted);
    return FTRs_Go_To_Next_Action;
}

#endif // !_WIN32

void ftest_state_corruption_fuzz_pre_start()
{
    fe_computer_players = 1;
}

TbBool ftest_state_corruption_fuzz_init()
{
#ifndef _WIN32
    ftest_append_action(ftest_state_corruption_fuzz_action001, 0, NULL);
#else
    FTESTLOG("state_corruption_fuzz needs fork(): not run on Windows");
#endif
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
