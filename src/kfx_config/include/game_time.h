// A plain data struct: elapsed in-game time, filled in by get_game_time()/update_game_time()
// (kfx_frontend/front_input.c) and read by the sim (player_utils.c), the in-game timer
// (frontmenu_ingame_evnt.c) and kfx_sim_state.GameT. Split out of sim_feedback.h so
// kfx_sim_state.h can hold one by value without pulling in the whole callback table.
#ifndef DK_GAME_TIME_H
#define DK_GAME_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

struct GameTime {
    unsigned char Seconds;
    unsigned char Minutes;
    unsigned char Hours;
};

#ifdef __cplusplus
}
#endif
#endif
