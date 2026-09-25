#include "ftest_packet_capture.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "ftest.h"

#include "config_keeperfx.h"
#include "packet_data.h"
#include "player_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static struct FtestCapturedPacket s_buf[FTEST_PACKET_CAPTURE_MAX];
static int64_t s_count = 0;
static TbBool s_recording = 0;
static TbBool s_have_last_turn = 0;
static GameTurn s_last_turn = 0; /* capture at most once per game turn */

void ftest_packet_capture_begin(void)
{
    s_count = 0;
    s_have_last_turn = 0;
    s_recording = 1;
}

void ftest_packet_capture_end(void)
{
    s_recording = 0;
}

void ftest_packet_capture_reset(void)
{
    s_count = 0;
    s_have_last_turn = 0;
    s_recording = 0;
}

/* A command packet the GUI (or any local input) actually produced. Empty
 * turns -- mouse-move-only, or PCtr_Gui-hover with no action -- are not
 * recorded, so a trace is exactly the sequence of player actions and
 * stays comparable across the classic/ImGui paths regardless of where the
 * cursor happened to rest. */
static TbBool packet_is_interesting(const struct Packet *p)
{
    return p->action != PckA_None;
}

void ftest_packet_capture_tick(void)
{
    if (!s_recording)
        return;

    const GameTurn turn = get_gameturn();
    if (s_have_last_turn && turn == s_last_turn)
        return;
    s_last_turn = turn;
    s_have_last_turn = 1;

    const struct Packet *p = &sim_packets[my_player_number];
    if (!packet_is_interesting(p))
        return;

    if (s_count >= FTEST_PACKET_CAPTURE_MAX)
    {
        FTESTLOG("buffer full (%" PRId64 "), dropping turn %" PRIu64, (int64_t)(FTEST_PACKET_CAPTURE_MAX), (uint64_t)turn);
        return;
    }

    s_buf[s_count].turn = turn;
    memcpy(&s_buf[s_count].packet, p, sizeof(struct Packet));
    s_count++;
}

int64_t ftest_packet_capture_count(void)
{
    return s_count;
}

const struct FtestCapturedPacket *ftest_packet_capture_at(int64_t idx)
{
    if (idx < 0 || idx >= s_count)
        return NULL;
    return &s_buf[idx];
}

void ftest_packet_capture_dump(void)
{
    FTESTLOG("captured %" PRId64 " packet(s)", (int64_t)(s_count));
    for (int64_t i = 0; i < s_count; i++)
    {
        const struct Packet *p = &s_buf[i].packet;
        FTESTLOG("  [%" PRId64 "] turn=%" PRIu64 " action=%" PRIu64 " par=%" PRId64 "/%" PRId64 "/%" PRId64 "/%" PRId64 " ctrl=%08" PRIx64 " gui=%" PRId64,
                 (int64_t)(i), (uint64_t)s_buf[i].turn, (uint64_t)p->action,
                 (int64_t)p->actn_par1, (int64_t)p->actn_par2, (int64_t)p->actn_par3, (int64_t)p->actn_par4,
                 (uint64_t)p->control_flags, (int64_t)((p->control_flags & PCtr_Gui) != 0));
    }
}

static TbBool par_ok(int64_t want, int64_t got)
{
    return (want == FTEST_PKT_ANY) || (want == got);
}

static TbBool pars_ok(const struct Packet *p, int64_t par1, int64_t par2, int64_t par3, int64_t par4)
{
    return par_ok(par1, p->actn_par1) && par_ok(par2, p->actn_par2)
        && par_ok(par3, (int64_t)p->actn_par3) && par_ok(par4, (int64_t)p->actn_par4);
}

TbBool ftest_packet_expect_once(unsigned char action,
                                int64_t par1, int64_t par2, int64_t par3, int64_t par4)
{
    int64_t matches = 0;
    for (int64_t i = 0; i < s_count; i++)
    {
        const struct Packet *p = &s_buf[i].packet;
        if (p->action == action && pars_ok(p, par1, par2, par3, par4))
            matches++;
    }
    if (matches != 1)
    {
        FTEST_FAIL_TEST("expected exactly 1 packet with action=%" PRIu64 " pars=%" PRId64 "/%" PRId64 "/%" PRId64 "/%" PRId64 ", found %" PRId64,
                        (uint64_t)action, (int64_t)par1, (int64_t)par2, (int64_t)par3, (int64_t)par4, (int64_t)(matches));
        ftest_packet_capture_dump();
        return 0;
    }
    return 1;
}

TbBool ftest_packet_expect_absent(unsigned char action)
{
    for (int64_t i = 0; i < s_count; i++)
    {
        if (s_buf[i].packet.action == action)
        {
            FTEST_FAIL_TEST("expected no packet with action=%" PRIu64 ", found one at turn %" PRIu64,
                            (uint64_t)action, (uint64_t)s_buf[i].turn);
            ftest_packet_capture_dump();
            return 0;
        }
    }
    return 1;
}

TbBool ftest_packet_trace_matches(const struct FtestPacketExpectation *expect, int64_t n)
{
    int64_t ai = 0;
    for (int64_t i = 0; i < s_count; i++)
    {
        const struct Packet *p = &s_buf[i].packet;
        if (p->action == PckA_None)
            continue;

        if (ai >= n)
        {
            FTEST_FAIL_TEST("trace has more than %" PRId64 " action packet(s)", (int64_t)(n));
            ftest_packet_capture_dump();
            return 0;
        }

        const struct FtestPacketExpectation *e = &expect[ai];
        const TbBool gui_ok = !e->require_gui_flag || ((p->control_flags & PCtr_Gui) != 0);
        if (p->action != e->action || !pars_ok(p, e->par1, e->par2, e->par3, e->par4) || !gui_ok)
        {
            FTEST_FAIL_TEST("trace[%" PRId64 "] mismatch: got action=%" PRIu64 " pars=%" PRId64 "/%" PRId64 "/%" PRId64 "/%" PRId64 " gui=%" PRId64,
                            (int64_t)(ai), (uint64_t)p->action,
                            (int64_t)p->actn_par1, (int64_t)p->actn_par2, (int64_t)p->actn_par3, (int64_t)p->actn_par4,
                            (int64_t)((p->control_flags & PCtr_Gui) != 0));
            ftest_packet_capture_dump();
            return 0;
        }
        ai++;
    }
    if (ai != n)
    {
        FTEST_FAIL_TEST("trace has %" PRId64 " action packet(s), expected %" PRId64, (int64_t)(ai), (int64_t)(n));
        ftest_packet_capture_dump();
        return 0;
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
