#include "ftest_packet_inject.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <string.h>

#include "ftest.h"

#include "config_keeperfx.h"
#include "packet_data.h"
#include "player_data.h"
#include "kfx_sim_state.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static struct FtestPacketInject s_pending[PACKETS_COUNT];
static TbBool s_has_pending[PACKETS_COUNT];

void ftest_packet_inject_queue(NetUserId user, const struct FtestPacketInject *req)
{
    if (user < 0 || user >= PACKETS_COUNT || req == NULL)
    {
        FTEST_FAIL_TEST("ftest_packet_inject_queue: bad user %" PRId64, (int64_t)user);
        return;
    }
    s_pending[user] = *req;
    s_has_pending[user] = 1;
}

void ftest_packet_inject_queue_local(const struct FtestPacketInject *req)
{
    ftest_packet_inject_queue(get_local_user(), req);
}

void ftest_packet_inject_reset(void)
{
    memset(s_has_pending, 0, sizeof(s_has_pending));
}

static void apply(const struct FtestPacketInject *r, struct Packet *p)
{
    if (r->action != PckA_None)
        set_packet_action(p, r->action, r->par1, r->par2, r->par3, r->par4);
    if (r->has_position)
    {
        set_players_packet_position(p, r->pos_x, r->pos_y, r->context);
        /* A position on the map means the cursor is not over a panel; input() flags PCtr_Gui
         * because the headless cursor rests over the sidebar, and the click dispatch ignores
         * the whole packet while it is set. */
        p->control_flags &= ~PCtr_Gui;
    }
    p->control_flags |= r->control_flags;
}

void ftest_packet_inject_tick_before_exchange(void)
{
    // in a multiplayer game the local player's packet must be what goes over the wire, or the other machines never
    // see the action (and the game desyncs): written before the exchange, as input() writes it
    if (kfx_sim_state.game_kind != GKind_MultiGame)
        return;
    const NetUserId user = get_local_user();
    if ((user < 0) || (user >= PACKETS_COUNT) || !s_has_pending[user])
        return;
    s_has_pending[user] = 0;
    apply(&s_pending[user], get_local_packet());
}

void ftest_packet_inject_tick(void)
{
    for (NetUserId user = 0; user < PACKETS_COUNT; user++)
    {
        if (!s_has_pending[user])
            continue;
        s_has_pending[user] = 0;
        apply(&s_pending[user], &sim_packets[user]);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
