#include "ftest_net_fake.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include <stdlib.h>
#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FTestNetFakeMsg {
    struct FTestNetFakeMsg *next;
    size_t size;
    char data[1]; // sized via malloc(sizeof(...) - 1 + size) below
};

static struct FTestNetFakeMsg *ftest_net_fake_inbox_head[MAX_NET_USERS];
static struct FTestNetFakeMsg *ftest_net_fake_inbox_tail[MAX_NET_USERS];

enum FTestNetFakeRole {
    FTestNetFakeRole_Host,
    FTestNetFakeRole_Client,
};
static enum FTestNetFakeRole ftest_net_fake_active_role = FTestNetFakeRole_Host;
static NetUserId ftest_net_fake_client_id = 1;

static NetUserId ftest_net_fake_my_mailbox(void)
{
    return (ftest_net_fake_active_role == FTestNetFakeRole_Host) ? SERVER_ID : ftest_net_fake_client_id;
}

static void ftest_net_fake_enqueue(NetUserId mailbox, const char *buffer, size_t size)
{
    if (mailbox < 0 || mailbox >= MAX_NET_USERS) {
        return;
    }
    struct FTestNetFakeMsg *msg = (struct FTestNetFakeMsg *)malloc(sizeof(struct FTestNetFakeMsg) - 1 + size);
    if (msg == NULL) {
        return;
    }
    msg->next = NULL;
    msg->size = size;
    if (size > 0) {
        memcpy(msg->data, buffer, size);
    }
    if (ftest_net_fake_inbox_tail[mailbox] != NULL) {
        ftest_net_fake_inbox_tail[mailbox]->next = msg;
    } else {
        ftest_net_fake_inbox_head[mailbox] = msg;
    }
    ftest_net_fake_inbox_tail[mailbox] = msg;
}

static TbError ftest_net_fake_init(NetDropCallback drop_callback, NetNewUserCallback new_user_callback)
{
    (void)drop_callback;
    (void)new_user_callback;
    return Lb_OK;
}

static void ftest_net_fake_exit(void)
{
}

static TbError ftest_net_fake_host(const char *session, void *options)
{
    (void)session;
    (void)options;
    return Lb_OK;
}

static TbError ftest_net_fake_join(const char *session, void *options)
{
    (void)session;
    (void)options;
    return Lb_OK;
}

static void ftest_net_fake_update(NetNewUserCallback new_user)
{
    (void)new_user;
}

static void ftest_net_fake_sendmsg_single(NetUserId destination, const char *buffer, size_t size)
{
    // `destination` selects a real transport's routing target; a fake
    // loopback only ever has one other party (whichever role isn't
    // currently active), so delivery is keyed by the sender's own
    // mailbox instead -- see the header comment for why.
    (void)destination;
    ftest_net_fake_enqueue(ftest_net_fake_my_mailbox(), buffer, size);
}

static void ftest_net_fake_sendmsg_single_unsequenced(NetUserId destination, const char *buffer, size_t size)
{
    ftest_net_fake_sendmsg_single(destination, buffer, size);
}

static void ftest_net_fake_sendmsg_all(const char *buffer, size_t size)
{
    ftest_net_fake_enqueue(ftest_net_fake_my_mailbox(), buffer, size);
}

static size_t ftest_net_fake_msgready(NetUserId source, uint64_t timeout)
{
    // Delivery is synchronous, so there's never anything to actually wait
    // for -- a real NetSP's `timeout` only matters when a message might
    // still be in flight.
    (void)timeout;
    if (source < 0 || source >= MAX_NET_USERS || ftest_net_fake_inbox_head[source] == NULL) {
        return 0;
    }
    return ftest_net_fake_inbox_head[source]->size;
}

static size_t ftest_net_fake_readmsg(NetUserId source, char *buffer, size_t max_size)
{
    if (source < 0 || source >= MAX_NET_USERS) {
        return 0;
    }
    struct FTestNetFakeMsg *msg = ftest_net_fake_inbox_head[source];
    if (msg == NULL) {
        return 0;
    }
    size_t copy_size = (msg->size < max_size) ? msg->size : max_size;
    if (copy_size > 0) {
        memcpy(buffer, msg->data, copy_size);
    }
    ftest_net_fake_inbox_head[source] = msg->next;
    if (ftest_net_fake_inbox_head[source] == NULL) {
        ftest_net_fake_inbox_tail[source] = NULL;
    }
    free(msg);
    return copy_size;
}

static void ftest_net_fake_drop_user(NetUserId id, enum NetJoinRejection reason)
{
    (void)id;
    (void)reason;
}

static struct NetSP ftest_net_fake_sp_instance = {
    .init = ftest_net_fake_init,
    .exit = ftest_net_fake_exit,
    .host = ftest_net_fake_host,
    .join = ftest_net_fake_join,
    .update = ftest_net_fake_update,
    .sendmsg_single = ftest_net_fake_sendmsg_single,
    .sendmsg_single_unsequenced = ftest_net_fake_sendmsg_single_unsequenced,
    .sendmsg_all = ftest_net_fake_sendmsg_all,
    .msgready = ftest_net_fake_msgready,
    .readmsg = ftest_net_fake_readmsg,
    .drop_user = ftest_net_fake_drop_user,
};

struct NetSP *ftest_net_fake_sp(void)
{
    return &ftest_net_fake_sp_instance;
}

void ftest_net_fake_set_role_host(void)
{
    ftest_net_fake_active_role = FTestNetFakeRole_Host;
}

void ftest_net_fake_set_role_client(NetUserId as_id)
{
    ftest_net_fake_active_role = FTestNetFakeRole_Client;
    ftest_net_fake_client_id = as_id;
}

void ftest_net_fake_reset(void)
{
    for (int64_t i = 0; i < MAX_NET_USERS; i++) {
        struct FTestNetFakeMsg *msg = ftest_net_fake_inbox_head[i];
        while (msg != NULL) {
            struct FTestNetFakeMsg *next = msg->next;
            free(msg);
            msg = next;
        }
        ftest_net_fake_inbox_head[i] = NULL;
        ftest_net_fake_inbox_tail[i] = NULL;
    }
    ftest_net_fake_active_role = FTestNetFakeRole_Host;
    ftest_net_fake_client_id = 1;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
