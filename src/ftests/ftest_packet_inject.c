#include "ftest_packet_inject.h"

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

void ftest_packet_inject_tick(void)
{
    for (NetUserId user = 0; user < PACKETS_COUNT; user++)
    {
        if (!s_has_pending[user])
            continue;
        s_has_pending[user] = 0;
        const struct FtestPacketInject *r = &s_pending[user];
        struct Packet *p = &sim_packets[user];
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
}

#ifdef __cplusplus
}
#endif

#endif /* FUNCTESTING */
