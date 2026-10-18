#include "ftest_camera_keys_seat.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"

#include "bflib_keybrd.h"
#include "config_keeperfx.h"
#include "config_settings.h"
#include "external_seat.h"
#include "kfx_sim_state.h"
#include "local_camera.h"
#include "net_game.h"
#include "player_data.h"
#include "player_instances.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

static int64_t s_failures = 0;
#define SOFT_FAIL(...) do { s_failures++; FTESTLOG("CHECK FAILED: " __VA_ARGS__); } while (0)

static int64_t s_frames;
static MapCoord s_x0, s_y0;
static TbKeyCode s_key;

static void note_start(void);
static void check_moved(const char *what);
FTestActionResult cks00_zoom(struct FTestActionArgs* const args);
FTestActionResult cks00_claim(struct FTestActionArgs* const args);
FTestActionResult cks00_hold(struct FTestActionArgs* const args);
FTestActionResult cks00_check(struct FTestActionArgs* const args);
// What the level start does for every keeper: the heart-zoom intro, which locks this user's input until it ends
// 16 turns on. An agent claiming the seat while it runs (the bridge's connect, right after the level loads) moves
// the player to another user.
FTestActionResult cks00_zoom(struct FTestActionArgs* const args)
{
    set_player_instance(get_my_player(), PI_HeartZoom, 0);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks00_claim(struct FTestActionArgs* const args)
{
    net_add_external_seat(my_player_number);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks00_hold(struct FTestActionArgs* const args)
{
    note_start();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks00_check(struct FTestActionArgs* const args)
{
    check_moved("seat claimed during the heart zoom");
    net_release_external_seat(my_player_number);
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks01_hold(struct FTestActionArgs* const args);
FTestActionResult cks02_check_plain(struct FTestActionArgs* const args);
FTestActionResult cks03_claim_hold(struct FTestActionArgs* const args);
FTestActionResult cks04_check_seat(struct FTestActionArgs* const args);
FTestActionResult cks05_pause_hold(struct FTestActionArgs* const args);
FTestActionResult cks07_release_hold(struct FTestActionArgs* const args);

static void note_start(void)
{
    struct Camera *cam = get_local_active_camera(get_my_player());
    s_x0 = cam->mappos.x.val;
    s_y0 = cam->mappos.y.val;
    lbKeyOn[s_key] = 1;
}

static void check_moved(const char *what)
{
    lbKeyOn[s_key] = 0;
    struct Camera *cam = get_local_active_camera(get_my_player());
    const int64_t moved = llabs((int64_t)cam->mappos.x.val - (int64_t)s_x0) + llabs((int64_t)cam->mappos.y.val - (int64_t)s_y0);
    FTESTLOG("CAMKEYS: %s: camera moved %" PRId64 " (paused=%d seat=%d turn=%" PRIu64 ")", what, moved,
        flag_is_set(kfx_sim_state.operation_flags, GOF_Paused),
        flag_is_set(get_my_player()->allocflags, PlaF_ExternalSeat), (uint64_t)get_gameturn());
    if (moved < 256) {
        SOFT_FAIL("%s: the camera did not move with a movement key held", what);
    }
}

TbBool ftest_camera_keys_seat_init()
{
    s_failures = 0;
    s_frames = 0;
    s_key = (TbKeyCode)settings.kbkeys[Gkey_MoveRight].code;
    ftest_append_action(cks00_zoom, 10, NULL);
    ftest_append_action(cks00_claim, 3, NULL);
    ftest_append_action(cks00_hold, 60, NULL);
    ftest_append_action(cks00_check, 30, NULL);
    ftest_append_action(cks01_hold, 10, NULL);
    ftest_append_action(cks02_check_plain, 30, NULL);
    ftest_append_action(cks03_claim_hold, 5, NULL);
    ftest_append_action(cks04_check_seat, 30, NULL);
    ftest_append_action(cks05_pause_hold, 5, NULL);
    ftest_append_action(cks07_release_hold, 0, NULL);
    return true;
}

FTestActionResult cks01_hold(struct FTestActionArgs* const args)
{
    note_start();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks02_check_plain(struct FTestActionArgs* const args)
{
    check_moved("plain game");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks03_claim_hold(struct FTestActionArgs* const args)
{
    net_add_external_seat(my_player_number);
    note_start();
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks04_check_seat(struct FTestActionArgs* const args)
{
    check_moved("External seat on the local keeper");
    return FTRs_Go_To_Next_Action;
}

// The game turn does not advance while paused, so the paused phases count calls (one per frame) themselves.
#define PAUSED_PHASE_FRAMES 60

FTestActionResult cks05_pause_hold(struct FTestActionArgs* const args)
{
    if (s_frames == 0) {
        extseat_pause();
        note_start();
    }
    if (++s_frames < PAUSED_PHASE_FRAMES) {
        return FTRs_Repeat_Current_Action;
    }
    s_frames = 0;
    check_moved("External seat, game paused");
    return FTRs_Go_To_Next_Action;
}

FTestActionResult cks07_release_hold(struct FTestActionArgs* const args)
{
    if (s_frames == 0) {
        net_release_external_seat(my_player_number);
        note_start();
    }
    if (++s_frames < PAUSED_PHASE_FRAMES) {
        return FTRs_Repeat_Current_Action;
    }
    s_frames = 0;
    check_moved("seat released, game still paused");
    extseat_resume();
    if (s_failures > 0) { FTEST_FAIL_TEST("%" PRId64 " camera key check(s) failed", s_failures); return FTRs_Go_To_Next_Action; }
    FTESTLOG("Test passed: a held movement key moves the camera in a plain game, with an External seat, and while paused.");
    return FTRs_Go_To_Next_Action;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
