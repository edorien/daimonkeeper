/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file replay_rewind.c
 *     Rewinding a replay while watching it (replay_rewind.h).
 * @par Purpose:
 *     Upstream #5450 (and #5470), re-implemented over this fork's state blobs.
 *     Keyframes are taken every REWIND_KEYFRAME_INTERVAL turns while a replay
 *     plays; the turns between are reached by playing on from the keyframe
 *     before (kfx_apploop's replay_rewind_to_turn()).
 * @par Comment:
 *     Upstream snapshots `game`, Ariadne's triangles and the globals it tags
 *     into a linker section. Here the state is what a full network resync
 *     carries, visited piece by piece by kfx_net's resync_visit_state(): the
 *     state structs, Ariadne's mesh, the configuration's saved part. A
 *     snapshot buffer holds those pieces as they were at one keyframe; each
 *     keyframe record in the ring (rewind_ring.h) holds the XOR diff from the
 *     keyframe before it (so the snapshot walks both ways along the chain),
 *     the Lua state (serialised, compressed) and the replay reader's place.
 *     No kernel memory tracking: comparing the live pieces against the
 *     snapshot page by page is fast enough (the keyframe log line says how long
 *     it takes).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "replay_rewind.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "bflib_datetm.h"
#include "config.h"
#include "config_keeperfx.h"
#include "creature_states_combt.h"
#include "engine_redraw.h"
#include "engine_textures.h"
#include "game_replay.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "local_camera.h"
#include "local_state.h"
#include "net_resync.h"
#include "packet_data.h"
#include "player_data.h"
#include "ports/script_port.h"
#include "ports/ui_port.h"
#include "rewind_ring.h"
#include "thing_data.h"
#include "thing_list.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define REWIND_LOG_INTERVAL 1024
#define REWIND_MAX_PIECES 64

/* A piece of the state (resync_visit_state()) and where it sits in the snapshot: each starts on a page. */
struct RewindPiece {
    unsigned char *ptr;
    size_t len;
    size_t snap_off;
};

/* A ring record: this header, then diff_len bytes of diff, then lua_len bytes of compressed Lua state. */
struct RewindKeyframe {
    GameTurn turn;
    uint64_t diff_len;
    uint64_t lua_len;
    uint64_t lua_raw_len;
    struct ReplayReaderPos reader;
};
#define KEYFRAME_HEADER_SIZE ((sizeof(struct RewindKeyframe) + 7) & ~(size_t)7)

static struct RewindPiece pieces[REWIND_MAX_PIECES];
static int pieces_count;
static TbBool pieces_overflow;
static size_t snapshot_len;
/* the pieces as they were at keyframe snapshot_turn */
static unsigned char *snapshot;
static GameTurn snapshot_turn;
static struct RewindRing ring;
/* a keyframe record is put together here, then copied into the ring */
static struct RewindBuf stage;
/* set when something failed: no more keyframes until the next replay */
static TbBool rewind_disabled;
/* a recorded resync was applied since the newest keyframe */
static TbBool resync_since_keyframe;
/* for the log line */
static int64_t capture_ns_total;
static int64_t capture_ns_max;
static int64_t capture_count;
static size_t last_record_len;
static size_t largest_record_len;
/******************************************************************************/

static void collect_piece(void *ctx, int part, void *data, size_t size)
{
    (void)ctx;
    (void)part;
    if (pieces_count >= REWIND_MAX_PIECES)
    {
        pieces_overflow = true;
        return;
    }
    struct RewindPiece *p = &pieces[pieces_count++];
    p->ptr = (unsigned char *)data;
    p->len = size;
    p->snap_off = snapshot_len;
    snapshot_len += rewind_page_round(size);
}

static void free_buffers(void)
{
    rewind_ring_free(&ring);
    rewind_buf_free(&stage);
    free(snapshot);
    snapshot = NULL;
    snapshot_len = 0;
    snapshot_turn = 0;
    pieces_count = 0;
    pieces_overflow = false;
}

static void rewind_fail(const char *what)
{
    ERRORLOG("Replay rewind: %s; rewinding is off for this replay", what);
    free_buffers();
    rewind_disabled = true;
}

static TbBool init_buffers(void)
{
    if (snapshot != NULL)
        return true;
    pieces_count = 0;
    pieces_overflow = false;
    snapshot_len = 0;
    resync_visit_state(collect_piece, NULL);
    if (pieces_overflow || (snapshot_len == 0))
    {
        rewind_fail("too many state pieces");
        return false;
    }
    uint64_t kb = replay_rewind_buffer_kb;
    if (kb > REPLAY_REWIND_BUFFER_MAX_KB)
        kb = REPLAY_REWIND_BUFFER_MAX_KB;
    snapshot = (unsigned char *)calloc(1, snapshot_len);
    if ((snapshot == NULL) || !rewind_ring_init(&ring, (size_t)kb * 1024))
    {
        rewind_fail("cannot allocate its buffers");
        return false;
    }
    JUSTLOG("Replay rewind: %d state pieces, %.1f MB snapshot, %.1f MB of keyframes", pieces_count,
        snapshot_len / (1024.0 * 1024.0), kb / 1024.0);
    return true;
}

static struct RewindKeyframe keyframe_at(size_t i, const unsigned char **rec)
{
    struct RewindKeyframe kf;
    const unsigned char *r = rewind_ring_record(&ring, i, NULL);
    memcpy(&kf, r, sizeof(kf));
    if (rec != NULL)
        *rec = r;
    return kf;
}

static GameTurn keyframe_turn(size_t i)
{
    return keyframe_at(i, NULL).turn;
}

/* The newest keyframe at or before turn; 0 (the oldest) if they are all after it. */
static size_t keyframe_index_at(GameTurn turn)
{
    size_t lo = 0;
    size_t hi = ring.count;
    while (hi - lo > 1)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (keyframe_turn(mid) <= turn)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

/* Walks the snapshot along the chain of diffs to keyframe target: each diff is the XOR of its keyframe and the one
 * before, so going back applies the newer keyframes' diffs and going forward the older ones'. */
static TbBool move_snapshot_to(size_t target)
{
    size_t cur = keyframe_index_at(snapshot_turn);
    if (keyframe_turn(cur) != snapshot_turn)
        return false;
    while (cur != target)
    {
        const size_t apply = (target > cur) ? cur + 1 : cur;
        const unsigned char *rec;
        const struct RewindKeyframe kf = keyframe_at(apply, &rec);
        if (!rewind_diff_apply(snapshot, snapshot_len, rec + KEYFRAME_HEADER_SIZE, kf.diff_len))
            return false;
        cur = (target > cur) ? cur + 1 : cur - 1;
    }
    snapshot_turn = keyframe_turn(target);
    return true;
}

static TbBool capture_lua(struct RewindKeyframe *kf)
{
    if (!script_lua_script_active())
        return true;
    size_t raw_len = 0;
    const char *data = script_lua_resync_export(&raw_len);
    TbBool ok = (data != NULL);
    if (ok && (raw_len > 0))
    {
        uLongf dst_len = compressBound(raw_len);
        ok = rewind_buf_reserve(&stage, dst_len)
            && (compress2(stage.data + stage.len, &dst_len, (const Bytef *)data, raw_len, Z_BEST_SPEED) == Z_OK);
        if (ok)
        {
            stage.len += dst_len;
            kf->lua_len = dst_len;
            kf->lua_raw_len = raw_len;
        }
    }
    script_cleanup_serialized_data();
    return ok;
}

static TbBool restore_lua(const struct RewindKeyframe *kf, const unsigned char *rec)
{
    if (!script_lua_script_active())
        return true;
    if (kf->lua_raw_len > 0)
    {
        char *data = (char *)malloc(kf->lua_raw_len);
        if (data == NULL)
            return false;
        uLongf len = kf->lua_raw_len;
        const TbBool ok = (uncompress((Bytef *)data, &len, rec + KEYFRAME_HEADER_SIZE + kf->diff_len, kf->lua_len) == Z_OK)
            && (len == kf->lua_raw_len) && script_lua_resync_import(data, len);
        free(data);
        if (!ok)
            return false;
    }
    // as after a resync (finish_resync())
    script_lua_set_random_seed(kfx_sim_state.action_random_seed);
    return true;
}

void replay_rewind_capture(void)
{
    if (!replay.load_enable || (replay_rewind_buffer_kb == 0) || rewind_disabled)
        return;
    const GameTurn turn = get_gameturn();
    if (ring.count > 0)
    {
        const GameTurn newest = keyframe_turn(ring.count - 1);
        if ((turn <= newest) || ((turn < newest + REWIND_KEYFRAME_INTERVAL) && !resync_since_keyframe))
            return;
    }
    if (!init_buffers())
        return;
    const int64_t start_ns = get_time_tick_ns();
    struct RewindKeyframe kf;
    memset(&kf, 0, sizeof(kf));
    kf.turn = turn;
    // while a recorded resync waits for the end of this turn there is no place between turns to come back to
    if (!replay_save_reader_pos(&kf.reader))
        return;
    stage.len = 0;
    if (!rewind_buf_reserve(&stage, KEYFRAME_HEADER_SIZE))
    {
        rewind_fail("no memory for a keyframe");
        return;
    }
    stage.len = KEYFRAME_HEADER_SIZE;
    const TbBool whole = (ring.count == 0);
    if (whole)
    {
        for (int i = 0; i < pieces_count; i++)
            memcpy(snapshot + pieces[i].snap_off, pieces[i].ptr, pieces[i].len);
    } else
    {
        if (!move_snapshot_to(ring.count - 1))
        {
            rewind_fail("cannot rebuild the newest keyframe");
            return;
        }
        for (int i = 0; i < pieces_count; i++)
        {
            const struct RewindPiece *p = &pieces[i];
            if (!rewind_diff_encode(&stage, snapshot + p->snap_off, p->ptr, p->len, p->snap_off))
            {
                rewind_fail("no memory for a keyframe's diff");
                return;
            }
        }
    }
    kf.diff_len = stage.len - KEYFRAME_HEADER_SIZE;
    if (!capture_lua(&kf))
    {
        rewind_fail("cannot keep the Lua state");
        return;
    }
    memcpy(stage.data, &kf, sizeof(kf));
    // the snapshot is now this keyframe's: it must make it into the ring
    unsigned char *rec = rewind_ring_push(&ring, stage.len);
    if (rec == NULL)
    {
        rewind_fail("REPLAY_REWIND_BUFFER_SIZE is too small for a keyframe");
        return;
    }
    memcpy(rec, stage.data, stage.len);
    snapshot_turn = turn;
    resync_since_keyframe = false;
    last_record_len = stage.len;
    if (stage.len > largest_record_len)
        largest_record_len = stage.len;
    const int64_t ns = get_time_tick_ns() - start_ns;
    capture_count++;
    // the first keyframe is a whole copy, the rest are diffs: those are what a keyframe costs as the replay plays
    if (!whole)
    {
        capture_ns_total += ns;
        if (ns > capture_ns_max)
            capture_ns_max = ns;
    }
    if ((capture_count == 1) || (turn / REWIND_LOG_INTERVAL != (turn - REWIND_KEYFRAME_INTERVAL) / REWIND_LOG_INTERVAL))
    {
        JUSTLOG("Replay rewind buffer: %.1f/%.0f MB, %d keyframes back to turn %" PRIu64 "; the last %.1f kB in %.2f ms, "
            "%.2f ms a keyframe on average", ring.used / (1024.0 * 1024.0), ring.cap / (1024.0 * 1024.0),
            (int)ring.count, (uint64_t)keyframe_turn(0), last_record_len / 1024.0, ns / 1e6,
            (capture_count > 1) ? capture_ns_total / 1e6 / (double)(capture_count - 1) : ns / 1e6);
    }
}

TbBool replay_rewind_restore(GameTurn target)
{
    if ((ring.count == 0) || (snapshot == NULL))
        return false;
    const size_t idx = keyframe_index_at(target);
    if (!move_snapshot_to(idx))
    {
        rewind_fail("cannot rebuild a keyframe");
        return false;
    }
    const unsigned char *rec;
    const struct RewindKeyframe kf = keyframe_at(idx, &rec);
    struct PlayerInfo *player = get_my_player();
    const int64_t view_mode = player->view_mode;
    const unsigned char texture_id = kfx_config_state.texture_id;
    // the things' sound emitters are this machine's, and the restored things don't own them
    stop_all_things_playing_samples();
    const struct LocalPacingState pacing = local_pacing_keep();
    for (int i = 0; i < pieces_count; i++)
        memcpy(pieces[i].ptr, snapshot + pieces[i].snap_off, pieces[i].len);
    local_pacing_restore(&pacing);
    for (ThingIndex i = 0; i < THINGS_COUNT; i++)
        thing_get(i)->snd_emitter_id = 0;
    resync_state_restored();
    reset_postal_instance_cache();
    if (!restore_lua(&kf, rec) || !replay_restore_reader_pos(&kf.reader))
    {
        rewind_fail("cannot restore a keyframe");
        return false;
    }
    // this machine's view of the restored game
    player = get_my_player();
    if (!local_state.replay_detached)
        init_local_cameras(player);
    if (player->view_mode != view_mode)
        set_engine_view(player, player->view_mode);
    if (kfx_config_state.texture_id != texture_id)
    {
        load_texture_map_file(kfx_config_state.texture_id, get_loaded_level_number(), get_level_fgroup(get_loaded_level_number()));
        init_animating_texture_maps();
    }
    ui_panel_map_update(0, 0, kfx_sim_state.map_subtiles_x + 1, kfx_sim_state.map_subtiles_y + 1);
    return true;
}

void replay_rewind_note_resync(void)
{
    resync_since_keyframe = true;
}

TbBool replay_rewind_available(void)
{
    return replay.load_enable && (ring.count > 0);
}

TbBool replay_rewind_at_limit(void)
{
    return (ring.count > 0) && (get_gameturn() <= keyframe_turn(0));
}

size_t replay_rewind_buffer_used(void)
{
    return ring.used;
}

void replay_rewind_get_stats(struct ReplayRewindStats *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->snapshot_bytes = (snapshot != NULL) ? snapshot_len : 0;
    stats->ring_bytes = ring.cap;
    stats->ring_used = ring.used;
    stats->largest_record = largest_record_len;
    stats->captures = capture_count;
    stats->diff_ns_total = capture_ns_total;
    stats->diff_ns_max = capture_ns_max;
}

size_t replay_rewind_keyframe_count(GameTurn *oldest_turn)
{
    if (oldest_turn != NULL)
        *oldest_turn = (ring.count > 0) ? keyframe_turn(0) : 0;
    return ring.count;
}

void replay_rewind_reset(void)
{
    free_buffers();
    rewind_disabled = false;
    resync_since_keyframe = false;
    capture_ns_total = 0;
    capture_ns_max = 0;
    capture_count = 0;
    last_record_len = 0;
    largest_record_len = 0;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
